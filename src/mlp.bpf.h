#ifndef MLP_BPF_H
#define MLP_BPF_H

#include <vmlinux.h>

#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MIN(a, b) (((a) < (b)) ? (a) : (b))

/*
 * ═══════════════════════════════════════════════════════════════════
 *                   LINEAR LAYER (Matrix Multiplication)
 * ═══════════════════════════════════════════════════════════════════
 *
 * Computes: y = W × x
 *
 * Where:
 *   - W: Weight matrix (M × N) [rows × columns]
 *   - x: Input vector (N elements)
 *   - y: Output vector (M elements)
 *
 * Matrix dimensions:
 *   Layer 0: W is 32×6, x is 6, y is 32
 *   Layer 1: W is 32×32, x is 32, y is 32
 *   Layer 2: W is 2×32, x is 32, y is 2
 *
 * ───────────────────────────────────────────────────────────────────
 * FIXED-POINT ARITHMETIC (Q16.16 Format)
 * ───────────────────────────────────────────────────────────────────
 *
 * All values are in Q16.16 fixed-point format:
 *   - 16 bits for integer part
 *   - 16 bits for fractional part
 *   - Total: 32 bits (int32_t)
 *
 * Example: 1.5 in float = 98304 in Q16.16 (1.5 × 65536)
 *
 * When multiplying two Q16.16 numbers:
 *   (a × 2^16) × (b × 2^16) = (a × b) × 2^32
 *
 * We need result in Q16.16, so divide by 2^16:
 *   result = (a × b) × 2^32 / 2^16 = (a × b) × 2^16
 *
 * Right-shift by 16 is equivalent to dividing by 2^16:
 *   sum >> 16  ←  Fast integer division
 *
 * ───────────────────────────────────────────────────────────────────
 * EXAMPLE CALCULATION
 * ───────────────────────────────────────────────────────────────────
 *
 * Input: x = [1.5, -0.5, 2.0] in float
 *        x = [98304, -32768, 131072] in Q16.16
 *
 * Weights: W[0] = [0.1, 0.2, 0.3] in float
 *          W[0] = [6554, 13107, 19661] in Q16.16
 *
 * Dot product:
 *   sum = (6554 × 98304) + (13107 × -32768) + (19661 × 131072)
 *   sum = 644349696 - 429490176 + 2577006592
 *   sum = 2791866112 (int64_t, Q32.32 format)
 *
 * Shift right by 16 to get Q16.16:
 *   y[0] = 2791866112 >> 16 = 42598 (≈ 0.65 in float)
 *
 * Verification: 1.5×0.1 + (-0.5)×0.2 + 2.0×0.3 = 0.15 - 0.1 + 0.6 = 0.65 ✓
 *
 * ───────────────────────────────────────────────────────────────────
 * PERFORMANCE OPTIMIZATION
 * ───────────────────────────────────────────────────────────────────
 *
 * #pragma clang loop unroll(full)
 *   - Tells compiler to unroll the outer loop completely
 *   - Generates more code but avoids loop overhead
 *   - Critical for eBPF which has strict instruction limits
 *   - Inner loop cannot be unrolled (too many instructions)
 */
static inline void linear_layer(const int32_t *W, const int32_t *x, int32_t *y, const int32_t M, const int32_t N)
{
    int64_t sum;

    // Unroll outer loop for performance (eBPF requirement)
    #pragma clang loop unroll(full)
    for (int32_t i = 0; i < M; ++i)  // For each output neuron
    {
        sum = 0;

        // Compute dot product: sum = W[i,:] · x
        for (int32_t j = 0; j < N; ++j)  // For each input
        {
            // Multiply and accumulate
            // Cast to int64_t to prevent overflow (32bit × 32bit = 64bit)
            sum += ((int64_t)(W[i * N + j])) * ((int64_t)(x[j]));
        }

        // Convert from Q32.32 to Q16.16 by right-shifting 16 bits
        // This is equivalent to dividing by 65536 but much faster
        y[i] = sum >> 16;
    }
}

/*
 * ═══════════════════════════════════════════════════════════════════
 *                   ReLU ACTIVATION FUNCTION
 * ═══════════════════════════════════════════════════════════════════
 *
 * ReLU = Rectified Linear Unit
 * Formula: f(x) = max(0, x)
 *
 * Graph:
 *      │
 *    5 │         ╱
 *    4 │        ╱
 *    3 │       ╱
 *    2 │      ╱
 *    1 │     ╱
 *    0 │────┘─────
 *      │-5 -3 -1 1 3 5
 *
 * ───────────────────────────────────────────────────────────────────
 * WHAT IT DOES
 * ───────────────────────────────────────────────────────────────────
 *
 * Applies element-wise transformation:
 *   - If value is positive: Keep it unchanged
 *   - If value is negative: Replace with zero
 *
 * Example:
 *   Input:  [-100, -50, 0, 50, 100]
 *   Output: [   0,   0, 0, 50, 100]
 *
 * ───────────────────────────────────────────────────────────────────
 * WHY WE NEED IT
 * ───────────────────────────────────────────────────────────────────
 *
 * Without activation functions, neural networks can only learn
 * LINEAR relationships. Multiple linear layers = single linear layer!
 *
 *   Linear(A) + Linear(B) = Linear(A+B)
 *
 * ReLU adds NON-LINEARITY, allowing the network to learn complex
 * patterns like:
 *   - "Attack IF (many packets AND long duration)"
 *   - "Benign IF (few packets OR short duration)"
 *
 * These boolean/conditional patterns require non-linear functions.
 *
 * ───────────────────────────────────────────────────────────────────
 * WHY ReLU (vs Sigmoid/Tanh)?
 * ───────────────────────────────────────────────────────────────────
 *
 * Advantages:
 *   1. FAST: Just a comparison (no expensive exp/division)
 *   2. No gradient vanishing (unlike sigmoid/tanh)
 *   3. Sparse activation (many neurons output 0)
 *   4. Works extremely well in practice
 *
 * Disadvantages:
 *   - "Dying ReLU": Neurons can get stuck at 0
 *   - Not used in output layer (we need negative scores)
 *
 * ───────────────────────────────────────────────────────────────────
 * WHERE IT'S APPLIED
 * ───────────────────────────────────────────────────────────────────
 *
 * In this network, ReLU is applied TWICE:
 *
 *   Input (6) → Linear → [ReLU #1] → Hidden1 (32)
 *   Hidden1 (32) → Linear → [ReLU #2] → Hidden2 (32)
 *   Hidden2 (32) → Linear → Output (2)  [No ReLU here!]
 *
 * Note: NO ReLU after the final layer because:
 *   - We need to compare BENIGN vs ATTACK scores
 *   - Both can be negative (logits, not probabilities)
 *   - ReLU would make them all positive, losing information
 */
static inline void relu(int32_t *tensor, const int32_t size)
{
    // Unroll loop for performance (eBPF requirement)
    #pragma clang loop unroll(full)
    for (int32_t i = 0; i < size; i++)
    {
        // If tensor[i] < 0, replace with 0
        // If tensor[i] >= 0, keep it unchanged
        tensor[i] = MAX(tensor[i], 0);
    }
}

/*
 * ═══════════════════════════════════════════════════════════════════
 *                   LEAKY ReLU ACTIVATION FUNCTION (IMPROVED)
 * ═══════════════════════════════════════════════════════════════════
 *
 * LeakyReLU = Leaky Rectified Linear Unit
 * Formula: f(x) = max(alpha * x, x) where alpha = 0.01
 *
 * Graph:
 *      │
 *    5 │         ╱
 *    4 │        ╱
 *    3 │       ╱
 *    2 │      ╱
 *    1 │     ╱
 *    0 │    ╱
 *      │   ╱────────
 *      │-5 -3 -1 1 3 5
 *        ╱  (small slope for negative values)
 *
 * ───────────────────────────────────────────────────────────────────
 * IMPROVEMENT OVER ReLU
 * ───────────────────────────────────────────────────────────────────
 *
 * ReLU Problem: "Dying ReLU"
 *   - If a neuron's output becomes negative during training
 *   - ReLU makes it exactly 0
 *   - Gradient becomes 0
 *   - Neuron NEVER recovers (permanently "dead")
 *
 * LeakyReLU Solution:
 *   - For negative values: f(x) = 0.01 * x (small slope)
 *   - Gradient is 0.01 instead of 0
 *   - Neuron can still learn and recover
 *
 * Example:
 *   Input:  [-100, -50, 0, 50, 100]
 *   ReLU:   [   0,   0, 0, 50, 100]  ← Negative info lost!
 *   LeakyReLU: [-1, -0.5, 0, 50, 100]  ← Negative info preserved!
 *
 * ───────────────────────────────────────────────────────────────────
 * FIXED-POINT IMPLEMENTATION
 * ───────────────────────────────────────────────────────────────────
 *
 * alpha = 0.01 in Q16.16 fixed-point:
 *   0.01 * 65536 = 655.36 ≈ 655
 *
 * For negative x:
 *   result = (x * 655) >> 16
 *
 * Example: x = -65536 (which is -1.0 in Q16.16)
 *   ((-65536) * 655) >> 16
 *   = -42926080 >> 16
 *   = -655 (which is -0.01 in Q16.16)
 *
 * ───────────────────────────────────────────────────────────────────
 * WHY LeakyReLU IS BETTER FOR INTRUSION DETECTION
 * ───────────────────────────────────────────────────────────────────
 *
 * 1. Robustness to unusual attacks:
 *    - Novel attacks might trigger negative activations
 *    - LeakyReLU preserves this information
 *    - ReLU would zero it out, losing detection signal
 *
 * 2. Better gradient flow:
 *    - All neurons remain trainable
 *    - Improves convergence speed
 *    - Better final accuracy
 *
 * 3. Minimal overhead:
 *    - Only slightly more expensive than ReLU
 *    - One multiplication + one shift per negative value
 *    - Still eBPF-compatible (no division/exp needed)
 *
 * ───────────────────────────────────────────────────────────────────
 * COMPATIBILITY NOTE
 * ───────────────────────────────────────────────────────────────────
 *
 * This function is fully compatible with eBPF because:
 *   - Uses only integer arithmetic
 *   - No floating point operations
 *   - No division (uses right-shift instead)
 *   - Loop can be fully unrolled (size is compile-time constant)
 *
 * To use LeakyReLU instead of ReLU, replace calls to relu() with
 * leaky_relu() in the XDP tail call programs.
 */
static inline void leaky_relu(int32_t *tensor, const int32_t size)
{
    // Alpha = 0.01 in Q16.16 fixed-point format
    // 0.01 * 65536 = 655.36 ≈ 655
    const int32_t alpha_fixed = 655;

    // Unroll loop for performance (eBPF requirement)
    #pragma clang loop unroll(full)
    for (int32_t i = 0; i < size; i++)
    {
        if (tensor[i] < 0)
        {
            // For negative values: f(x) = alpha * x
            // Multiply by alpha (Q16.16), then shift right by 16
            // Cast to int64_t to prevent overflow
            int64_t result = ((int64_t)tensor[i] * (int64_t)alpha_fixed) >> 16;
            tensor[i] = (int32_t)result;
        }
        // For positive values: keep unchanged (tensor[i] >= 0)
    }
}

/*
 * ═══════════════════════════════════════════════════════════════════
 *                   STANDARD SCALER (Normalization)
 * ═══════════════════════════════════════════════════════════════════
 *
 * Formula: y = (x - mean) / scale
 *
 * This normalizes features to have mean=0 and similar variance.
 *
 * ───────────────────────────────────────────────────────────────────
 * WHY NORMALIZATION IS CRITICAL
 * ───────────────────────────────────────────────────────────────────
 *
 * Without normalization, features have wildly different scales:
 *
 *   Feature            Raw Value      Scale
 *   ──────────────────────────────────────────
 *   max_packet_length  134           (0-1500)
 *   max_duration       7,951,539     (0-billions!)
 *   min_packet_length  32            (0-1500)
 *   dst_port           2080          (0-65535)
 *   header_length      168           (0-10000)
 *   num_packet         5             (0-10000+)
 *
 * Problem: Neural networks are SENSITIVE to scale!
 *   - Large values (like max_duration) DOMINATE the learning
 *   - Small values (like num_packet) get IGNORED
 *   - Weights become unbalanced
 *   - Training becomes unstable
 *
 * After normalization, all features are on similar scale (-3 to +3):
 *
 *   Feature            Normalized
 *   ───────────────────────────────
 *   max_packet_length  -0.97
 *   max_duration       -0.46
 *   min_packet_length  -0.07
 *   dst_port           +0.95
 *   header_length      -0.24
 *   num_packet         -0.24
 *
 * Now all features contribute EQUALLY to the neural network!
 *
 * ───────────────────────────────────────────────────────────────────
 * HOW IT WORKS
 * ───────────────────────────────────────────────────────────────────
 *
 * Step 1: Calculate mean and scale from TRAINING DATA
 *   mean[i] = average of feature i across all training samples
 *   scale[i] = standard deviation of feature i
 *
 * Step 2: Apply transformation
 *   normalized = (raw - mean) / scale
 *
 * Example for max_packet_length:
 *   raw = 134 bytes
 *   mean = 572 bytes (average from training data)
 *   scale = 451 bytes (standard deviation from training data)
 *
 *   normalized = (134 - 572) / 451 = -438 / 451 = -0.97
 *
 * ───────────────────────────────────────────────────────────────────
 * IMPLEMENTATION DETAILS
 * ───────────────────────────────────────────────────────────────────
 *
 * Input:
 *   - x[]: Raw feature values (int64_t, large range)
 *   - mean[]: Mean from training (int64_t)
 *   - scale[]: Standard deviation from training (int64_t)
 *
 * Output:
 *   - y[]: Normalized features (int32_t, Q16.16 fixed-point)
 *
 * Why two branches (if/else)?
 *   - Avoid integer underflow when x < mean
 *   - If mean > x: Result is negative, compute |mean-x| then negate
 *   - If mean <= x: Result is positive, compute x-mean directly
 *
 * Fixed-point conversion:
 *   - Multiply by (1 << 16) = 65536 to convert to Q16.16
 *   - Example: -0.97 in float → -63646 in Q16.16
 *
 * ───────────────────────────────────────────────────────────────────
 * REAL EXAMPLE FROM LOGS (Mohammad's curl)
 * ───────────────────────────────────────────────────────────────────
 *
 * Feature 0 (max_packet_length):
 *   x[0] = 134
 *   mean[0] = 572
 *   scale[0] = 451
 *
 *   Step 1: mean > x, so compute -(mean - x) / scale
 *   Step 2: (572 - 134) = 438
 *   Step 3: 438 * 65536 = 28704768
 *   Step 4: 28704768 / 451 = 63646
 *   Step 5: Negate: -63646
 *
 *   y[0] = -63646 (Q16.16) ≈ -0.97 (float)
 *
 * Feature 1 (max_duration):
 *   x[1] = 7,951,539
 *   mean[1] = 11,161,066,668
 *   scale[1] = 24,027,934,608
 *
 *   Result: y[1] = -30420 ≈ -0.46 (float)
 *
 * ───────────────────────────────────────────────────────────────────
 * CRITICAL REQUIREMENT
 * ───────────────────────────────────────────────────────────────────
 *
 * The mean and scale values MUST come from the SAME training data
 * that was used to train the neural network!
 *
 * If you retrain the model, you MUST also update mean/scale:
 *   1. python3 mlp_train.py  (saves mean/scale to mlp.th)
 *   2. python3 mlp_quant.py mlp.th 16  (exports to mlp_params.bpf.h)
 *   3. make  (recompiles with new parameters)
 *
 * Mismatched mean/scale → Network sees wrong inputs → Wrong classifications!
 */
static inline void standard_scaler(int64_t *x, int32_t *y, int64_t *mean, int64_t *scale, int32_t N)
{
    // Unroll loop for all 6 features
    #pragma clang loop unroll(full)
    for (int32_t i = 0; i < N; ++i)
    {
        // Branch based on whether result will be negative or positive
        // This avoids integer underflow when x < mean

        if (mean[i] > x[i])
        {
            // Negative result: x is below average
            // Compute: -(mean - x) / scale, convert to Q16.16
            y[i] = -((int32_t)(((uint64_t)mean[i] - (uint64_t)x[i]) * (1 << 16) / (uint64_t)scale[i]));
        }
        else
        {
            // Positive result: x is above or at average
            // Compute: (x - mean) / scale, convert to Q16.16
            y[i] = (int32_t)(((uint64_t)x[i] - (uint64_t)mean[i]) * (1 << 16) / (uint64_t)scale[i]);
        }
    }
}

#endif