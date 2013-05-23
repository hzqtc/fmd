#! /bin/sh

wget -q -O - --post-data="email=doubantucao@gmail.com&password=doubantucao&app_name=radio_desktop_win&version=100" "http://www.douban.com/j/app/login" | python -mjson.tool > /tmp/login.json

echo "[DoubanFM]
channel = 0"
cat /tmp/login.json | grep user_id | awk '{print $2}' | awk -F'"' '{print "uid = "$2}'
cat /tmp/login.json | grep user_name | awk '{print $2}' | awk -F'"' '{print "uname = "$2}'
cat /tmp/login.json | grep token | awk '{print $2}' | awk -F'"' '{print "token = "$2}'
cat /tmp/login.json | grep expire | awk '{print $2}' | awk -F'"' '{print "expire = "$2}'
echo "kbps ="

echo "
[Output]
driver = alsa
device = default
rate = 44100

[Server]
address = 0.0.0.0
port = 10098
"
