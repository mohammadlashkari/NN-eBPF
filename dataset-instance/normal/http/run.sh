# httperf --server=33.33.33.220 --port=80 --uri=/index.html --rate=4 --num-conn=1024 --num-call=8 --timeout=5
ab -n 8192 -c 32 -k -s 5 http://33.33.33.73/index.html
