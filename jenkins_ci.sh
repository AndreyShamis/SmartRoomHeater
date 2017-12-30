info() {
  echo "\033[1;33m[Info]    $1  \033[0m"
}

error() {
  echo "\033[1;31m[Error]   $1  \033[0m"
}

success() {
  echo "\033[1;32m[Success] $1 \033[0m"
}

export ARDUINO_V="1.8.5"
export ARDUINO_V_FOLDER="arduino-${ARDUINO_V}"
export ARDUINO_TAR="${ARDUINO_V_FOLDER}-linux64.tar.xz"
export PATH_TO_TAR="../${ARDUINO_TAR}"
export ARDUINO_LIB="${JENKINS_HOME}/Arduino/libraries/"
rm -rf  "${JENKINS_HOME}/.arduino15"
rm -rf "${JENKINS_HOME}/Arduino"
success "WORKSPACE is ${WORKSPACE}"

success "Starting X server "
/sbin/start-stop-daemon --start --quiet --pidfile /tmp/custom_xvfb_1.pid --make-pidfile --background --exec /usr/bin/Xvfb -- :1 -ac -screen 0 1280x1024x16
sleep 2
export DISPLAY=:1.0

success "_####################### Installing ARDUINO_#######################"

if [ ! -f "${PATH_TO_TAR}" ]; then
	wget -O ${PATH_TO_TAR} http://downloads.arduino.cc/${ARDUINO_TAR}
else
	info "File  exist [ ${ARDUINO_TAR} in ${PATH_TO_TAR} ], using local file."
fi
tar xf ${PATH_TO_TAR}

success "_####################### Installing libraries_#######################"
mkdir -p ${ARDUINO_LIB}
./${ARDUINO_V_FOLDER}/arduino --pref preferences-file=${WORKSPACE}/.arduino/preferences.txt --save-prefs
#./${ARDUINO_V_FOLDER}/arduino --pref libraries=${WORKSPACE}/Arduino/libraries  --save-prefs
#./${ARDUINO_V_FOLDER}/arduino  --pref preferences-file=${WORKSPACE}/.arduino/preferences.txt --pref libraries=${WORKSPACE}/Arduino/libraries  --save-prefs

wget -O Arduino-Temperature-Control-Library-master.zip https://github.com/milesburton/Arduino-Temperature-Control-Library/archive/master.zip
wget -O NTPClient.zip https://github.com/arduino-libraries/NTPClient/archive/master.zip
wget -O ESP8266Ping.zip https://github.com/dancol90/ESP8266Ping/archive/master.zip
unzip Arduino-Temperature-Control-Library-master.zip && mv Arduino-Temperature-Control-Library-master ${ARDUINO_LIB}
unzip NTPClient.zip && mv NTPClient-master ${ARDUINO_LIB}
unzip ESP8266Ping.zip && mv ESP8266Ping-master ${ARDUINO_LIB}

success "_####################### Installing esp8266_#######################"
CURRENT_DIR=$PWD
info "Current location " ${CURRENT_DIR}
cd ${WORKSPACE}/${ARDUINO_V_FOLDER}/hardware
mkdir esp8266com
cd esp8266com
git clone https://github.com/esp8266/Arduino.git esp8266
cd esp8266/tools
python get.py
cd ${CURRENT_DIR}

success "_####################### Installing esp8266 in arduino _#######################"
./${ARDUINO_V_FOLDER}/arduino --board esp8266com:esp8266:generic --save-prefs
./${ARDUINO_V_FOLDER}/arduino --pref "boardsmanager.additional.urls=http://arduino.esp8266.com/stable/package_esp8266com_index.json" --save-prefs
./${ARDUINO_V_FOLDER}/arduino --install-boards esp8266:esp8266 --save-prefs

export BOARD_NODEMCU_SETTINGS=CpuFrequency=80,UploadSpeed=921600,FlashSize=4M3M
export BOARD_NODEMCUV2=esp8266:esp8266:nodemcuv2:${BOARD_NODEMCU_SETTINGS} #nodeMCU v2 - 1.0
export BOARD=${BOARD_NODEMCUV2}

echo "\n###############################################################\n"
echo "Checking ${BOARD}"
./${ARDUINO_V_FOLDER}/arduino --verify --board ${BOARD} --verbose SmartRoomHeater.ino

#./${ARDUINO_V_FOLDER}/arduino-builder -compile -logger=machine -hardware /var/lib/jenkins/workspaces/SmartRoomHeater/31/arduino-1.8.5/hardware -tools /var/lib/jenkins/workspaces/SmartRoomHeater/31/arduino-1.8.5/tools-builder -tools /var/lib/jenkins/workspaces/SmartRoomHeater/31/arduino-1.8.5/hardware/tools/avr -built-in-libraries /var/lib/jenkins/workspaces/SmartRoomHeater/31/arduino-1.8.5/libraries -libraries /var/lib/jenkins/Arduino/libraries -fqbn=esp8266:esp8266:nodemcuv2:CpuFrequency=80,UploadSpeed=921600,FlashSize=4M3M -ide-version=10805  -warnings=null -prefs=preferences-file=/var/lib/jenkins/workspaces/SmartRoomHeater/31/.arduino/preferences.txt -libraries=/var/lib/jenkins/workspaces/SmartRoomHeater/31/Arduino/libraries -prefs=build.warn_data_percentage=75  -verbose SmartRoomHeater.ino
echo "Finish ${BOARD}\n\n"
