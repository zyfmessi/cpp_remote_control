#include<stdio.h>
#include<iostream>
#include<string>
#include<Windows.h>
#pragma comment(lib,"ws2_32.lib")
int main()
{
	//1.初始化网络环境
	WSADATA wsadata;
	WSAStartup(MAKEWORD(2, 2), &wsadata);
	//创建服务器socket
	SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	//判断是否创建成功
	if (server_socket == INVALID_SOCKET)
	{
		std::cout << "创建失败" << std::endl;
		return 0;
	}
	//给服务器socket绑定地址，
	SOCKADDR_IN server_addr;//先准备一个地址
	server_addr.sin_family = AF_INET;//使用ipv4协议
	server_addr.sin_port = 9999;//监听端口
	server_addr.sin_addr.S_un.S_addr = inet_addr("0.0.0.0");
	if (bind(server_socket, (sockaddr*)&server_addr, sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		std::cout << "绑定服务器socket失败" << std::endl;
		return 0;
	}
	//开启服务器socket监听
	if (listen(server_socket, 1) == SOCKET_ERROR)
	{
		std::cout << "绑定服务器socket失败" << std::endl;
		return 0;
	}
	//等待客户端连接
	//准备客户端地址
	SOCKADDR_IN client_addr;
	int client_addr_length = sizeof(client_addr);
	SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_addr_length);//返回客户端socket

	return 0;
}