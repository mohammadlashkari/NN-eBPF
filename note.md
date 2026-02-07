 irq/167-iwlwifi-539     [008] ..s21  7078.732344: bpf_trace_printk: ==> Local network packet: 10.244.34.57:39756 -> 10.244.34.177:2080
 irq/167-iwlwifi-539     [008] ..s21  7078.741924: bpf_trace_printk: ==> Local network packet: 10.244.34.57:39756 -> 10.244.34.177:2080
 irq/167-iwlwifi-539     [008] ..s21  7078.741945: bpf_trace_printk: ==> Local network packet: 10.244.34.57:39756 -> 10.244.34.177:2080
 irq/167-iwlwifi-539     [008] ..s21  7078.753780: bpf_trace_printk: ==> Local network packet: 10.244.34.57:39756 -> 10.244.34.177:2080
 irq/167-iwlwifi-539     [008] ..s21  7078.753800: bpf_trace_printk: ==> Local network packet: 10.244.34.57:39756 -> 10.244.34.177:2080
 irq/167-iwlwifi-539     [008] ..s21  7078.753805: bpf_trace_printk: ==> Local network packet: 10.244.34.57:39756 -> 10.244.34.177:2080
 irq/167-iwlwifi-539     [008] ..s21  7078.753807: bpf_trace_printk: ==> Flow ending (FIN/RST) - Starting NN inference...
 irq/167-iwlwifi-539     [008] ..s21  7078.753810: bpf_trace_printk: ==> Packets in flow: 12, Starting preprocessing...
 irq/167-iwlwifi-539     [008] ..s21  7078.753813: bpf_trace_printk: [PREPROCESSING] Flow: 39756->2080, packets=12
 irq/167-iwlwifi-539     [008] ..s21  7078.753815: bpf_trace_printk: [PREPROCESSING] Features: max_len=132, min_len=32, duration=100817881247
 irq/167-iwlwifi-539     [008] ..s21  7078.753817: bpf_trace_printk: [PREPROCESSING] port=2080, hdr_len=400
 irq/167-iwlwifi-539     [008] ..s21  7078.753819: bpf_trace_printk: NN params loaded - first weight: -14419, mean[0]: 572
 irq/167-iwlwifi-539     [008] ..s21  7078.753821: bpf_trace_printk: [DEBUG] Raw features BEFORE normalization:
 irq/167-iwlwifi-539     [008] ..s21  7078.753823: bpf_trace_printk:   max_len=132, max_dur=100817881247, min_len=32
 irq/167-iwlwifi-539     [008] ..s21  7078.753825: bpf_trace_printk:   dst_port=2080, hdr_len=400, num_pkt=12
 irq/167-iwlwifi-539     [008] ..s21  7078.753826: bpf_trace_printk: [DEBUG] Normalized features AFTER scaling:
 irq/167-iwlwifi-539     [008] ..s21  7078.753828: bpf_trace_printk:   norm[0]=-63937, norm[1]=244538, norm[2]=-4854
 irq/167-iwlwifi-539     [008] ..s21  7078.753830: bpf_trace_printk:   norm[3]=62086, norm[4]=-15415, norm[5]=-15417
 irq/167-iwlwifi-539     [008] ..s21  7078.753831: bpf_trace_printk: [DEBUG] Normalization params:
 irq/167-iwlwifi-539     [008] ..s21  7078.753832: bpf_trace_printk:   mean: 572, 11161066668, 36
 irq/167-iwlwifi-539     [008] ..s21  7078.753834: bpf_trace_printk:   scale: 451, 24027934608, 54
 irq/167-iwlwifi-539     [008] ..s21  7078.753836: bpf_trace_printk: [PREPROCESSING] Normalization complete, chaining to input layer...
 irq/167-iwlwifi-539     [008] ..s21  7078.753838: bpf_trace_printk: [INPUT LINEAR] Computing layer 0: 6 inputs -> 32 neurons
 irq/167-iwlwifi-539     [008] ..s21  7078.753842: bpf_trace_printk: [INPUT RELU] Applying activation function (ReLU)...
 irq/167-iwlwifi-539     [008] ..s21  7078.753844: bpf_trace_printk: [HIDDEN LINEAR] Computing layer 1: 32 -> 32 neurons
 irq/167-iwlwifi-539     [008] ..s21  7078.753861: bpf_trace_printk: [HIDDEN RELU] Applying activation function (ReLU)...
 irq/167-iwlwifi-539     [008] ..s21  7078.753864: bpf_trace_printk: [OUTPUT LINEAR] Computing layer 2: 32 -> 2 outputs (BENIGN vs ATTACK)
 irq/167-iwlwifi-539     [008] ..s21  7078.753867: bpf_trace_printk: [THRESHOLD] Current detection threshold: 5000
 irq/167-iwlwifi-539     [008] ..s21  7078.753868: bpf_trace_printk: ========================================
 irq/167-iwlwifi-539     [008] ..s21  7078.753869: bpf_trace_printk: *** INTRUSION DETECTION RESULT ***
 irq/167-iwlwifi-539     [008] ..s21  7078.753872: bpf_trace_printk: Flow: 10.244.34.57:39756 -> 10.244.34.177:2080
 irq/167-iwlwifi-539     [008] ..s21  7078.753873: bpf_trace_printk: Packets in flow: 12
 irq/167-iwlwifi-539     [008] ..s21  7078.753875: bpf_trace_printk: Score [BENIGN]: -1045206
 irq/167-iwlwifi-539     [008] ..s21  7078.753876: bpf_trace_printk: Score [ATTACK]: 1045555
 irq/167-iwlwifi-539     [008] ..s21  7078.753878: bpf_trace_printk: Confidence margin: 2090761 (threshold: 5000)
 irq/167-iwlwifi-539     [008] ..s21  7078.753879: bpf_trace_printk: Attack probability: ~99%
 irq/167-iwlwifi-539     [008] ..s21  7078.753882: bpf_trace_printk: Confidence: VERY HIGH
 irq/167-iwlwifi-539     [008] ..s21  7078.753883: bpf_trace_printk: Classification: *** ATTACK DETECTED ***
 irq/167-iwlwifi-539     [008] ..s21  7078.753885: bpf_trace_printk: Avg feature extraction: 1232 ns
 irq/167-iwlwifi-539     [008] ..s21  7078.753886: bpf_trace_printk: Total detection time: 61335 ns
 irq/167-iwlwifi-539     [008] ..s21  7078.753887: bpf_trace_printk: ========================================













 irq/168-iwlwifi-540     [009] ..s21  7090.915131: bpf_trace_printk: ==> Local network packet: 10.244.34.57:52572 -> 10.244.34.177:2080
 irq/168-iwlwifi-540     [009] ..s21  7090.924273: bpf_trace_printk: ==> Local network packet: 10.244.34.57:52572 -> 10.244.34.177:2080
 irq/168-iwlwifi-540     [009] ..s21  7090.924290: bpf_trace_printk: ==> Local network packet: 10.244.34.57:52572 -> 10.244.34.177:2080
 irq/168-iwlwifi-540     [009] ..s21  7090.935823: bpf_trace_printk: ==> Local network packet: 10.244.34.57:52572 -> 10.244.34.177:2080
 irq/168-iwlwifi-540     [009] ..s21  7090.935844: bpf_trace_printk: ==> Local network packet: 10.244.34.57:52572 -> 10.244.34.177:2080
 irq/168-iwlwifi-540     [009] ..s21  7090.935849: bpf_trace_printk: ==> Local network packet: 10.244.34.57:52572 -> 10.244.34.177:2080
 irq/168-iwlwifi-540     [009] ..s21  7090.935851: bpf_trace_printk: ==> Flow ending (FIN/RST) - Starting NN inference...
 irq/168-iwlwifi-540     [009] ..s21  7090.935854: bpf_trace_printk: ==> Packets in flow: 6, Starting preprocessing...
 irq/168-iwlwifi-540     [009] ..s21  7090.935857: bpf_trace_printk: [PREPROCESSING] Flow: 52572->2080, packets=6
 irq/168-iwlwifi-540     [009] ..s21  7090.935860: bpf_trace_printk: [PREPROCESSING] Features: max_len=132, min_len=32, duration=11543616
 irq/168-iwlwifi-540     [009] ..s21  7090.935861: bpf_trace_printk: [PREPROCESSING] port=2080, hdr_len=200
 irq/168-iwlwifi-540     [009] ..s21  7090.935863: bpf_trace_printk: NN params loaded - first weight: -14419, mean[0]: 572
 irq/168-iwlwifi-540     [009] ..s21  7090.935865: bpf_trace_printk: [DEBUG] Raw features BEFORE normalization:
 irq/168-iwlwifi-540     [009] ..s21  7090.935867: bpf_trace_printk:   max_len=132, max_dur=11543616, min_len=32
 irq/168-iwlwifi-540     [009] ..s21  7090.935869: bpf_trace_printk:   dst_port=2080, hdr_len=200, num_pkt=6
 irq/168-iwlwifi-540     [009] ..s21  7090.935870: bpf_trace_printk: [DEBUG] Normalized features AFTER scaling:
 irq/168-iwlwifi-540     [009] ..s21  7090.935872: bpf_trace_printk:   norm[0]=-63937, norm[1]=-30410, norm[2]=-4854
 irq/168-iwlwifi-540     [009] ..s21  7090.935873: bpf_trace_printk:   norm[3]=62086, norm[4]=-15677, norm[5]=-15669
 irq/168-iwlwifi-540     [009] ..s21  7090.935874: bpf_trace_printk: [DEBUG] Normalization params:
 irq/168-iwlwifi-540     [009] ..s21  7090.935876: bpf_trace_printk:   mean: 572, 11161066668, 36
 irq/168-iwlwifi-540     [009] ..s21  7090.935878: bpf_trace_printk:   scale: 451, 24027934608, 54
 irq/168-iwlwifi-540     [009] ..s21  7090.935879: bpf_trace_printk: [PREPROCESSING] Normalization complete, chaining to input layer...
 irq/168-iwlwifi-540     [009] ..s21  7090.935882: bpf_trace_printk: [INPUT LINEAR] Computing layer 0: 6 inputs -> 32 neurons
 irq/168-iwlwifi-540     [009] ..s21  7090.935887: bpf_trace_printk: [INPUT RELU] Applying activation function (ReLU)...
 irq/168-iwlwifi-540     [009] ..s21  7090.935890: bpf_trace_printk: [HIDDEN LINEAR] Computing layer 1: 32 -> 32 neurons
 irq/168-iwlwifi-540     [009] ..s21  7090.935927: bpf_trace_printk: [HIDDEN RELU] Applying activation function (ReLU)...
 irq/168-iwlwifi-540     [009] ..s21  7090.935930: bpf_trace_printk: [OUTPUT LINEAR] Computing layer 2: 32 -> 2 outputs (BENIGN vs ATTACK)
 irq/168-iwlwifi-540     [009] ..s21  7090.935933: bpf_trace_printk: [THRESHOLD] Current detection threshold: 5000
 irq/168-iwlwifi-540     [009] ..s21  7090.935934: bpf_trace_printk: ========================================
 irq/168-iwlwifi-540     [009] ..s21  7090.935936: bpf_trace_printk: *** INTRUSION DETECTION RESULT ***
 irq/168-iwlwifi-540     [009] ..s21  7090.935939: bpf_trace_printk: Flow: 10.244.34.57:52572 -> 10.244.34.177:2080
 irq/168-iwlwifi-540     [009] ..s21  7090.935940: bpf_trace_printk: Packets in flow: 6
 irq/168-iwlwifi-540     [009] ..s21  7090.935941: bpf_trace_printk: Score [BENIGN]: 12647
 irq/168-iwlwifi-540     [009] ..s21  7090.935942: bpf_trace_printk: Score [ATTACK]: -65720
 irq/168-iwlwifi-540     [009] ..s21  7090.935944: bpf_trace_printk: Confidence margin: -78367 (threshold: 5000)
 irq/168-iwlwifi-540     [009] ..s21  7090.935947: bpf_trace_printk: Attack probability: ~31%
 irq/168-iwlwifi-540     [009] ..s21  7090.935951: bpf_trace_printk: Confidence: VERY LOW
 irq/168-iwlwifi-540     [009] ..s21  7090.935953: bpf_trace_printk: Classification: BENIGN (Normal Traffic)
 irq/168-iwlwifi-540     [009] ..s21  7090.935955: bpf_trace_printk: Avg feature extraction: 1376 ns
 irq/168-iwlwifi-540     [009] ..s21  7090.935956: bpf_trace_printk: Total detection time: 83280 ns
 irq/168-iwlwifi-540     [009] ..s21  7090.935957: bpf_trace_printk: ========================================



i want the hot threshold be smarter 
i want the hot_threshol be like this 
default be the same like now (150000)
sudo ./.output/hot_threshold 100 (if i passed a fixed number set that number like now)
but if i pass some flag or some args like smart sudo ./.output/hot_threshold --smart or smart Arg (pick better name instead of smart)
it use some mean or std deviation or some smart algorithm to pick a new and better threshold value 

