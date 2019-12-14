[ ! -d "plugins/" ] && mkdir plugins
g++ --std=c++17 -fPIC -I../include -I../dependencies/asio/asio/include ../src/Assist.cpp -lpthread -ldl -lstdc++fs -shared -oplugins/Assist.plugin