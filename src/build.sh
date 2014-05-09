
protoc --proto_path=../primeserver/src --cpp_out=./ ../primeserver/src/protocol.proto
mv protocol.pb.cc protocol.pb.cpp

cp protocol.pb.* ../primeserver/src/
cp protocol.pb.* ../baseclient/
cp protocol.pb.* ../../xpmclient/

rm protocol.pb.*

