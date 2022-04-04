default: compile

compile:
	gcc -pthread -o proxy ./tcp_proxy.c -g