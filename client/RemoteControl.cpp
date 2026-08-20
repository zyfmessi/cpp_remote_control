#include <iostream>
#include <winsock2.h>
#include <Windows.h>
#pragma comment(lib, "ws2_32.lib")
#include<atlimage.h>
#include<ShellScalingApi.h>
#pragma pack(push,1)
#define RECV_BUFFER_SIZE 1024*1024*1
bool allow_send = true;
SOCKET server_socket;
SOCKET connected_socket;
SOCKADDR_IN server_addr{};// 准备服务器地址信息
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

Packet* PackPacket(int cmd, const std::string& message)
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
	if (len < sizeof(PacketHeader))//// 这里只能先判断包头的位置够不够
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
	if (packet.header.body_len > 0)//下面来获取数据
	{
		//创建节后
		p_packet = reinterpret_cast<Packet*>(new char[sizeof(PacketHeader) + packet.header.body_len]);
		//拷贝用户数据
		memcpy(p_packet->body, buffer + index, packet.header.body_len);
		//拷贝包头
		memcpy(&p_packet->header, &packet.header, sizeof(PacketHeader));
		return p_packet;
	}
	else if (packet.header.body_len <= 0)
	{
		std::cout << "没有发现数据包" << std::endl;
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
	WSADATA wsaData;// 初始化 Winsock 网络环境
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);//为winsock指定版本和参数
	if (result != 0)
	{
		std::cout << "WSA开启失败" << std::endl;
		return 0;
	}
	server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 创建服务器监听 socket
	if (server_socket == INVALID_SOCKET)
	{
		std::cout << "创建 server socket 失败" << std::endl;
		WSACleanup();
		return 0;
	}
	server_addr.sin_family = AF_INET;// 设置地址族为 IPv4
	server_addr.sin_port = htons(9999);// 设置服务器监听端口为 9999，并转换成网络字节序
	server_addr.sin_addr.s_addr = INADDR_ANY;// 监听本机所有 IPv4 地址
	result = bind(server_socket, reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr));// 4. 将服务器 socket 绑定到指定的 IP 地址和端口
	if (result == SOCKET_ERROR)//判断一下，绑定失败就退出
	{
		std::cout << "绑定失败" << std::endl;
		closesocket(server_socket);
		WSACleanup();
		return 0;
	}
	result = listen(server_socket, 1);//  开启服务器监听，等待客户端连接
	if (result == SOCKET_ERROR)
	{
		std::cout << "监听失败" << std::endl;
		closesocket(server_socket);
		WSACleanup();
		return 0;
	}
	return 1;
}

enum CMD //做个枚举，方便定义常量
{
	CMD_SCREEN = 1,
	CMD_MOUSE = 2,
	CMD_KEYBOARD = 4,
	CMD_TestConnected = 3
};

int HandScreen(Packet* packet)
{
	CImage image;//获取本地屏幕,
	HDC hScreen = GetDC(nullptr);//拿到屏幕的上下文
	int bit_width = GetDeviceCaps(hScreen, BITSPIXEL);//拿到屏幕的像素位宽
	std::cout << "bit_width:" << bit_width << std::endl;
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);//处理屏幕的缩放问题
	int sWidth = GetSystemMetrics(SM_CXSCREEN);//屏幕的宽
	int sHeight = GetSystemMetrics(SM_CYSCREEN);//屏幕的高
	std::cout << "sWidth=" << sWidth << " " << "sHeight=" << sHeight << std::endl;
	image.Create(sWidth, sHeight, bit_width);//创建图片,
	HDC  hdc_image = image.GetDC();
	BitBlt(hdc_image, 0, 0, sWidth, sHeight, hScreen, 0, 0, SRCCOPY);//把屏幕的数据复制到image中
	image.ReleaseDC();//释放image 的HDC
	ReleaseDC(NULL, hScreen);//释放屏幕的Device Context
	image.Save(_T("test.png", ::Gdiplus::ImageFormatPNG));
	//使用网络进行发送

	return 0;
}

int HandMouse(Packet* packet)
{
	return 0;
}

int HandKeyBoard(Packet* packet)
{
	return 0;
}

int HandTestConnect(Packet* packet)
{
	return 0;
}

int HandCommand(Packet* packet)
{
	int ret = 0;
	switch (packet->header.cmd)
	{
	case CMD_SCREEN://屏幕
		ret = HandScreen(packet);
	case CMD_MOUSE://鼠标
		ret = HandMouse(packet);
	case CMD_KEYBOARD://键盘
		ret = HandKeyBoard(packet);
	case CMD_TestConnected://测试连接
		ret = HandTestConnect(packet);
	default:
		break;
	}
	return ret;
}

int main()
{
	HandScreen(nullptr);
	std::cout << "wait, please press Enter to go on" << std::endl;
	getchar();
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
	if (InitSocket() != 0)
	{
		std::cout << "启动网络初始化失败" << std::endl;
	}
	std::cout << "等待客户端连接..." << std::endl;
	SOCKADDR_IN client_addr{};// 准备一个结构体来保存连接进来的客户端地址信息
	int client_addr_length = sizeof(client_addr);
	connected_socket = accept(server_socket, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_length);// 等待客户端连接
	if (connected_socket == INVALID_SOCKET)// 判断 accept 是否成功
	{
		std::cout << "接受客户端消息失败" << std::endl;
		closesocket(server_socket);
		WSACleanup();
		return 0;
	}
	std::cout << "客户端连接成功" << std::endl;
	char* buffer = new char[(int)RECV_BUFFER_SIZE];// 创建接收缓冲区，用来保存客户端发送过来的数据
	static int index = 0;
	while (true)
	{
		//返回接受到的数据长度
		int recv_length = recv(connected_socket, buffer + index, RECV_BUFFER_SIZE - index, 0);
		if (recv_length > 0)
		{
			index = index + recv_length;//拿到真实接受的字节数长度，用来更新index
			while (true)
			{
				Packet* from_client_packet = ParsePacket(buffer, index);//按照定义的协议来接受客户端的数据
				if (from_client_packet == nullptr)
				{
					break;
				}
				index = index - GetPacketLength(from_client_packet);
				memmove(buffer, buffer + GetPacketLength(from_client_packet), index);//剩余的数据搬到buffer开头
				if (from_client_packet->header.cmd == 1)//cmd==1表示客户端要获取数据
				{
					//截取本地数据发送给客户端
					std::string send_message(from_client_packet->body, from_client_packet->header.body_len);
					Packet* packet = PackPacket(1, send_message);
					send(connected_socket, reinterpret_cast<char*>(packet), GetPacketLength(packet), 0);
					delete[]reinterpret_cast<char*>(packet);
					packet = nullptr;
				}
				std::cout.write(from_client_packet->body, from_client_packet->header.body_len);	// 不直接使用 cout << buffer，因为 recv 不保证在数据末尾自动添加 '\0'
				std::cout << "服务器收到信息 " << std::endl;
				std::string accpet_message(from_client_packet->body, from_client_packet->header.body_len);
				if (allow_send)// 将刚刚收到的数据重新打包发送回客户端
				{
					Packet* packet = PackPacket(from_client_packet->header.cmd, accpet_message);
					int send_length = send(connected_socket, reinterpret_cast<char*>(packet), GetPacketLength(packet), 0);
					// 判断发送是否失败)
					if (send_length == SOCKET_ERROR)
					{
						std::cout << "发送数据给客户端失败" << std::endl;
					}
					else
					{
						std::cout << "数据返回给客户端" << std::endl;
					}
					delete[]reinterpret_cast<char*>(packet);
					packet = nullptr;
				}
				if (from_client_packet != nullptr)
				{
					delete[]reinterpret_cast<char*>(from_client_packet);
					from_client_packet = nullptr;
				}
			}
		}
		else if (recv_length == 0)
		{
			std::cout << "客户端断开连接" << ":" << WSAGetLastError() << std::endl;
			break;
		}
		else
		{
			std::cout << "接受客户端数据失败" << std::endl;
		}
	}
	delete[]buffer;
	buffer = nullptr;
	closesocket(connected_socket);// 9. 关闭服务器与该客户端之间的通信 socket
	closesocket(server_socket);// 10. 关闭服务器监听 socket
	WSACleanup();// 11. 清理 Winsock 网络环境
	std::cout << "按回车键退出..." << std::endl;
	std::cin.get();
	return 0;
}