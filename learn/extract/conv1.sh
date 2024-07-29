export LD_LIBRARY_PATH=/home/yss/caffe_cpu/build/lib:
export PYTHONPATH=/home/yss/caffe_cpu/python:$PYTHONPATH

python3 ep_del_bn_scale_factor_version_short_auto.py ../snapshots/_iter_10000.caffemodel 
mv binary.txt w000000000003.txt
xz -z -9 -k w000000000003.txt
scp -p w000000000004.txt.xz yss@192.168.11.31:/home/yss/prg/furibisha/weight/

