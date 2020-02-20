cd ../
cp -r config_ycsb_synthetic.h config.h

# algorithm
alg=WOUND_WAIT
spin="true"
# [WW]
ww_starv_free="false"
# [CLV]
dynamic="true"
on=1
retire_read="false"

# workload
wl="YCSB"
req=16
synthetic=false
zipf=0.9
num_hs=0
pos=SPECIFIED
specified=0.1
fhs="WR"
shs="WR"
read_ratio=1
ordered="false"
flip=0
table_size="1024*1024*10"

# other
threads=16
profile="true"
cnt=100000 
penalty=50000

for i in 0 1 2 3 4
do
for alg in WOUND_WAIT NO_WAIT SILO WAIT_DIE
do
for zipf in 0.5 0.7 0.9 1.1
do
for read_ratio in 0.3 0.7
do
timeout 60 python test.py RETIRE_READ=${retire_read} FLIP_RATIO=${flip} SPECIFIED_RATIO=${specified} WW_STARV_FREE=${ww_starv_free} KEY_ORDER=$ordered READ_PERC=${read_ratio} NUM_HS=${num_hs} FIRST_HS=$fhs POS_HS=$pos DEBUG_TMP="false" DYNAMIC_TS=$dynamic CLV_RETIRE_ON=$on SPINLOCK=$spin REQ_PER_QUERY=$req DEBUG_PROFILING=$profile SYNTH_TABLE_SIZE=${table_size} WORKLOAD=${wl} CC_ALG=$alg THREAD_CNT=$threads MAX_TXN_PER_PART=$cnt ABORT_PENALTY=$penalty ZIPF_THETA=$zipf SYNTHETIC_YCSB=$synthetic 
done
done
done
done

cd experiments
python3 send_email.py ycsb_test
