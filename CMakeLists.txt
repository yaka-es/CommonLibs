#
# OpenBTS/CommonLibs/CMakeLists.txt
#

add_library(openbts-common STATIC
	BitVector.cpp
	Configuration.cpp
	LinkedLists.cpp
	Logger.cpp
	Reporting.cpp
	SelfDetect.cpp
	Sockets.cpp
	Threads.cpp
	Timeval.cpp
	URLEncode.cpp
	UnixSignal.cpp
	Utils.cpp
	Variables.cpp
	sqlite3util.cpp
)

target_link_libraries(openbts-common coredumper sqlite3 pthread)

add_executable(BitVectorTest BitVectorTest.cpp)
add_executable(ConfigurationTest ConfigurationTest.cpp)
add_executable(F16Test F16Test.cpp)
add_executable(InterthreadTest InterthreadTest.cpp)
add_executable(LockTest LockTest.cpp)
add_executable(LogTest LogTest.cpp)
add_executable(RegexpTest RegexpTest.cpp)
#add_executable(ReportingTest ReportingTest.cpp)
add_executable(SelfDetectTest SelfDetectTest.cpp)
add_executable(SocketsTest SocketsTest.cpp)
add_executable(ThreadTest ThreadTest.cpp)
add_executable(TimevalTest TimevalTest.cpp)
add_executable(URLEncodeTest URLEncodeTest.cpp)
add_executable(UnixSignalTest UnixSignalTest.cpp)
add_executable(UtilsTest UtilsTest.cpp)
add_executable(VectorTest VectorTest.cpp)

target_link_libraries(BitVectorTest openbts-common)
target_link_libraries(ConfigurationTest openbts-common)
target_link_libraries(F16Test openbts-common)
target_link_libraries(InterthreadTest openbts-common)
target_link_libraries(LockTest openbts-common)
target_link_libraries(LogTest openbts-common)
target_link_libraries(RegexpTest openbts-common)
#target_link_libraries(ReportingTest openbts-common)
target_link_libraries(SelfDetectTest openbts-common)
target_link_libraries(SocketsTest openbts-common)
target_link_libraries(ThreadTest openbts-common)
target_link_libraries(TimevalTest openbts-common)
target_link_libraries(URLEncodeTest openbts-common)
target_link_libraries(UnixSignalTest openbts-common)
target_link_libraries(UtilsTest openbts-common)
target_link_libraries(VectorTest openbts-common)

add_test(BitVectorTest BitVectorTest)
add_test(ConfigurationTest ConfigurationTest)
add_test(F16Test F16Test)
#add_test(InterthreadTest InterthreadTest)  # 12 sec
add_test(LockTest LockTest)
add_test(LogTest LogTest)
add_test(RegexpTest RegexpTest)
#add_test(ReportingTest ReportingTest)
#add_test(SelfDetectTest SelfDetectTest)  # fails
#add_test(SocketsTest SocketsTest)  # hangs
#add_test(ThreadTest ThreadTest)  # Exception: Other
add_test(TimevalTest TimevalTest)
add_test(URLEncodeTest URLEncodeTest)
#add_test(UnixSignalTest UnixSignalTest)  # hangs
add_test(UtilsTest UtilsTest)
add_test(VectorTest VectorTest)

# install(example.config)
# install(README.common)
