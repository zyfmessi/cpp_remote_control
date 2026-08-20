#include <iostream>
#include<string>
#include<Windows.h>
#pragma comment(lib,"ws2_32.lib")
#define RECV_BUFFER_len 1024*1024*1 //从服务器接受的数据长度
SOCKET client_socket;
SOCKADDR_IN server_addr{};// 准备服务器地址
int number = 1000;
bool accept_meaasge = true;
#pragma pack(push,1)
struct  PacketHeader
{
	int magic;//4个字节的报头
	int cmd;// 4个字节的命令号
	int body_len;//数据的长度
};
#pragma pack(pop)
struct Packet
{
	PacketHeader header;//数据包的包头
	char body[];//包数据
};
Packet* PackPacket(int cmd, const std::string message)
{
	int body_len = static_cast<int>(message.size());
	Packet* pck = reinterpret_cast<Packet*>(new char[sizeof(PacketHeader) + static_cast<int>(message.size())]);
	pck->header.magic = 0x55AA77CC;
	pck->header.cmd = cmd;
	pck->header.body_len = body_len;
	if (!message.empty())
	{
		memcpy(pck->body, message.data(), message.size());
	}
	return pck;
}

Packet* ParsePacket(char* buffer, int len)
{
	//p_packet 是一个 Packet* 指针，这个指针指向解析完成后的完整数据包。
	Packet packet;
	Packet* p_packet;
	int index = 0;
	if (len < sizeof(PacketHeader))
	{
		return nullptr;
	}
	//len就是buffer的长度，是外部传进来的
	for (; index + sizeof(int) <= len; index++)//这个for循环是在判断当前位置是不是包头
	{
		int start_head = index;//记录当前包头的位置
		if (*((int*)(buffer + index)) == 0x55AA77CC)//找到包头
		{
			if (len - start_head < sizeof(PacketHeader))//当前的buffer长度-包头开始的位置，判断剩下的buffer长度能不能装下一个PacketHeader
			{
				return nullptr;
			}
			packet.header.magic = (*(int*)(buffer + index));
			index += 4;
			packet.header.cmd = *(int*)(buffer + index);
			index += 4;
			packet.header.body_len = *(int*)(buffer + index);
			index += 4;
			break;
		}
	}
	if (packet.header.body_len > 0)//判断一下真实的数据长度，下面来获取数据
	{
		//创建节后
		p_packet = reinterpret_cast<Packet*>(new char[sizeof(PacketHeader) + packet.header.body_len]);
		//拷贝用户数据
		memcpy(p_packet->body, buffer + index, packet.header.body_len);
		//拷贝包头
		memcpy(&p_packet->header, &packet.header, sizeof(PacketHeader));
		return p_packet;
	}
	else
	{
		return nullptr;
	}
}

int GetPacketLength(Packet* p)
{
	if (p == nullptr)
	{
		return 0;
	}
	else
	{
		return  p->header.body_len + sizeof(PacketHeader);
	}
}

int InitSocket()
{
	WSADATA wsadata;// 1. 初始化 Winsock
	int result = WSAStartup(MAKEWORD(2, 2), &wsadata);
	if (result != 0)
	{
		std::cout << "WSA开启失败: " << result << std::endl;
		return 0;
	}
	client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 创建客户端 socket
	if (client_socket == INVALID_SOCKET)
	{
		std::cout << "创建客户端socket失败，错误码：" << WSAGetLastError() << std::endl;
		WSACleanup();
		return 0;
	}
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9999);
	server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
}

int main()
{
	InitSocket();
	int result = connect(client_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));	// 4. 连接服务器
	if (result == SOCKET_ERROR)
	{
		std::cout << "客户端连接服务器失败，错误码：" << WSAGetLastError() << std::endl;
		closesocket(client_socket);
		WSACleanup();
		return 0;
	}
	std::cout << "客户端连接服务器成功" << std::endl;
	int count = 0;
	char* recv_buffer = new char[static_cast<int>(RECV_BUFFER_len)];//申请内存空间
	int index = 0;
	while (count < number)// 7. 等待服务器返回数据
	{
		std::string message = "packet:" + std::to_string(count);// 准备要发送的数据
		Packet* packet = PackPacket(2000, message);
		int send_length = send(client_socket, (char*)&packet->header.magic, packet->header.body_len + sizeof(PacketHeader), 0);// 6. 向服务器发送数据
		if (send_length == SOCKET_ERROR)
		{
			std::cout << "发送失败，错误码：" << WSAGetLastError() << std::endl;
			closesocket(client_socket);
			WSACleanup();
			return 0;
		}
		std::cout << "发送成功:" << message << " " << "发送字节数:" << send_length << std::endl;
		delete[] reinterpret_cast<char*>(packet);//释放buffer_packet区域
		packet = nullptr;
		// ========================
		// 接收服务器数据
		// ========================
		if (accept_meaasge)//决定是否从服务器接受数据
		{
			count++;
			int recv_length = recv(client_socket, recv_buffer + index, RECV_BUFFER_len - index, 0);
			if (recv_length > 0)
			{
				index = index + recv_length;
				while (true)
				{
					Packet* recv_packet = ParsePacket(recv_buffer, index);
					if (recv_packet == nullptr)
					{
						break;
					}
					int packet_length = GetPacketLength(recv_packet);
					std::cout << "服务器返回数据：";
					std::cout.write(recv_packet->body, recv_packet->header.body_len);
					std::cout << std::endl;
					index = index - packet_length;
					if (index > 0)
					{
						memmove(recv_buffer, recv_buffer + packet_length, index);
					}

					delete[]reinterpret_cast<char*>(recv_packet);
					recv_packet = nullptr;
					//if (recv_packet->header.cmd == 1)//解析显示收到的服务器传来的数据
					//{
					//}
				}
			}
			else if (recv_length == 0)
			{
				std::cout << "服务器关闭连接" << std::endl;
				break;
			}
			else
			{
				std::cout << "接收失败，错误码：" << WSAGetLastError() << std::endl;
			}
		}
	}
	delete[] recv_buffer;
	recv_buffer = nullptr;
	closesocket(client_socket);	//  关闭客户端 socket
	WSACleanup();// 清理 Winsock
	return 0;
}