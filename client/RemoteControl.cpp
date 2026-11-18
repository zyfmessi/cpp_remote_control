#include <iostream>
#include <winsock2.h>
#include <Windows.h>
#pragma comment(lib, "ws2_32.lib")
#include<atlimage.h>
#include<ShellScalingApi.h>

#pragma pack(push,1)
#define RECV_BUFFER_SIZE 1024*1024*10
// ===============================
// 屏幕通道：截图
// ===============================
#define SCREEN_PORT 10000
SOCKET screen_listen_socket = INVALID_SOCKET;// 负责监听 10000
SOCKET screen_connected_socket = INVALID_SOCKET;// accept 成功以后，真正传输屏幕
// ===============================
// 控制通道：鼠标 + 键盘
// ===============================
#define CONTROL_PORT 9999
SOCKET control_listen_socket = INVALID_SOCKET;// 负责监听 9999
SOCKET control_connected_socket = INVALID_SOCKET;// accept 成功以后，真正和控制端通信

//定义数据包的包头
struct  PacketHeader
{
	int magic;//4个字节的报头
	int cmd;// 4个字节的命令号
	int body_len;//数据的长度
};
#pragma pack(pop)

//定义数据包
struct Packet
{
	PacketHeader header;//数据包的包头
	char body[];//包数据
};

//数据包包头装填
Packet* PackPacket(int cmd, const char* data, int len)
{
	Packet* pck = reinterpret_cast<Packet*>(new char[sizeof(PacketHeader) + len]);
	pck->header.magic = 0x55AA77CC;
	pck->header.cmd = cmd;
	pck->header.body_len = len;

	if (data && len > 0)
	{
		memcpy(pck->body, data, len);
	}

	return pck;
}

//判断当前 buffer 里的数据能不能组成一个完整 Packet；
// 如果可以，就解析并返回一个完整 Packet；如果不够，就返回
Packet* ParsePacket(char* buffer, int len)
{
	//p_packet 是一个 Packet* 指针，这个指针指向解析完成后的完整数据包。
	PacketHeader packet_header = {};
	Packet* p_packet = nullptr;
	int index = 0;
	if (len < sizeof(PacketHeader))//// 这里只能先判断包头的位置够不够
	{
		std::cout << "包头的位置不够" << std::endl;
		return nullptr;
	}
	bool found_header = false;
	//len就是buffer的长度，是外部传进来的
	for (; index + sizeof(int) <= len; index++)//这个for循环是在判断当前位置是不是包头
	{
		int start_head = index;//记录当前包头的位置
		if (*((int*)(buffer + index)) == 0x55AA77CC)//找到包头
		{
			found_header = true;
			if (len - start_head < sizeof(PacketHeader))//当前的buffer长度-包头开始的位置，判断剩下的buffer长度能不能装下一个PacketHeader
			{
				std::cout << "剩下的buffer长度不能装下一个PacketHeader" << std::endl;
				return nullptr;
			}
			//给包头进行赋值
			packet_header.magic = *(int*)(buffer + index);
			index += 4;
			packet_header.cmd = *(int*)(buffer + index);
			index += 4;
			packet_header.body_len = *(int*)(buffer + index);
			index += 4;
			break;
		}
	}

	if (!found_header)
	{
		std::cout << "没有发现包头" << std::endl;
		return nullptr;
	}

	if (packet_header.body_len < 0)
	{
		std::cout << "数据长度错误" << std::endl;
		return nullptr;
	}
	if (len - index < packet_header.body_len)
	{
		std::cout << "正文没有接收完整" << std::endl;
		return nullptr;
	}
	p_packet = reinterpret_cast<Packet*>(new char[sizeof(PacketHeader) + packet_header.body_len]);//创建内存空间
	memcpy(&p_packet->header, &packet_header, sizeof(PacketHeader));//拷贝包头
	if (packet_header.body_len > 0)//下面来获取数据
	{
		memcpy(p_packet->body, buffer + index, packet_header.body_len);//拷贝正文数据
	}
	return p_packet;
}

//获取数据包的长度
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

//创建监听的socket
int CreateListenSocket(SOCKET& listen_socket, int port)
{
	// 创建 TCP socket
	listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 创建监听 用的socket
	if (listen_socket == INVALID_SOCKET)
	{
		std::cout << "创建 server socket 失败" << "端口：" << port << std::endl;
		return 0;
	}
	SOCKADDR_IN addr{};// 准备服务器地址信息
	addr.sin_family = AF_INET;// 设置地址族为 IPv4
	addr.sin_port = htons(port);// 设置服务器监听端口为 9999，并转换成网络字节序
	addr.sin_addr.s_addr = INADDR_ANY;// 监听本机所有 IPv4 地址
	//绑定IP+端口  将监听的socket 绑定到指定的 IP 地址和端口
	int result = bind(listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
	if (result == SOCKET_ERROR)//判断一下，绑定失败就退出
	{
		std::cout << "绑定失败，端口：" << port << "  " << "错误码：" << WSAGetLastError() << std::endl;
		closesocket(listen_socket);
		//WSACleanup();
		return 0;
	}
	//  开启监听，等待连接
	result = listen(listen_socket, 1);
	if (result == SOCKET_ERROR)
	{
		std::cout << "监听失败，端口：" << port << std::endl;
		closesocket(listen_socket);
		//WSACleanup();
		return 0;
	}
	return 1;
}

//项目中所有发送数据的地方都用这个函数,
int SendAll(SOCKET socket, const char* data, int all_data_length)
{
	int sent = 0;
	while (sent < all_data_length)//检查数据长度
	{
		int ret = send(socket, data + sent, all_data_length - sent, 0);//发送剩余数据
		if (ret == SOCKET_ERROR)
		{
			std::cout << "send失败，错误码：" << WSAGetLastError() << std::endl;
			return -1;
		}
		if (ret == 0)
		{
			return -1;
		}
		sent = sent + ret;// 本次成功发送 ret 字节
	}
	return sent;
}

//初始网络环境所有的socket
int InitSocket()
{
	WSADATA wsaData;// 初始化 Winsock 网络环境
	int result = WSAStartup(MAKEWORD(2, 2), &wsaData);//为winsock指定版本和参数
	if (result != 0)
	{
		std::cout << "WSA开启失败" << std::endl;
		return 0;
	}
	// ==========================
   // 1. 创建控制监听socket
   // 端口9999
   // ==========================
	if (CreateListenSocket(control_listen_socket, 9999) == 0)
	{
		WSACleanup();
		return 0;
	}
	// ==========================
	// 2. 创建屏幕监听socket
	// 端口10000
	// ==========================
	if (CreateListenSocket(screen_listen_socket, 10000) == 0)
	{
		closesocket(control_listen_socket);//释放socket
		WSACleanup();
		return 0;
	}
	std::cout
		<< "控制端口9999监听成功"
		<< std::endl;

	std::cout
		<< "屏幕端口10000监听成功"
		<< std::endl;

	return 1;
}

//屏幕，鼠标，键盘控制状态的枚举
enum CMD
{
	CMD_SCREEN = 1,
	CMD_MOUSE = 2,
	CMD_KEYBOARD = 4,
	CMD_TestConnected = 3
};

//鼠标的枚举常量
enum ENUM_MOUSE
{
	MOUSE_MOVE = 1,//移动
	MOUSE_LDOWN = 2,//左键按下
	MOUSE_LUP = 3,//左键抬起
	MOUSE_RDOWN = 4,//右键按下
	MOUSE_RUP = 5,//右键抬起
	MOUSE_MDOWN = 6,//中间按下
	MOUSE_MUP = 7,//中间抬起
	MOUSE_LCLICK = 8,//左键单击
	MOUSE_RCLICK = 9,//右键单击
	MOUSE_MCLICK = 10,//中间单击
	MOUSE_LDCLICK = 11,//左键双击
	MOUSE_RDCLICK = 12,//右键双击
	MOUSE_MDCLICK = 13,//鼠标中键双击
};

//鼠标的结构体
struct  Mouse
{
	int action;//鼠标行为，左键按下，抬起，双击等等, 上面enmu定义的
	POINT ptXY;//鼠标的坐标, x,y 坐标
};

//操作鼠标
int HandMouse(Packet* packet)
{
	if (packet->header.body_len != sizeof(Mouse))
	{
		std::cout
			<< "鼠标数据长度错误，收到："
			<< packet->header.body_len
			<< "，正确长度应该是："
			<< sizeof(Mouse)
			<< std::endl;

		return -1;
	}
	Mouse mouse{};
	//把网络数据包中的数据复制到Mouse结构体中
	memcpy(&mouse.action, packet->body, packet->header.body_len);
	//std::cout << "x=" << mouse.ptXY.x << "y=" << mouse.ptXY.y << " action=" << mouse.action << std::endl; \
		//模拟鼠标事件设置鼠标位置
		SetCursorPos(mouse.ptXY.x, mouse.ptXY.y);
	switch (mouse.action)
	{
	case MOUSE_MOVE://移动
		SetCursorPos(mouse.ptXY.x, mouse.ptXY.y);
		break;
	case	 MOUSE_LDOWN://单击左键按下
		mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, GetMessageExtraInfo());
		break;
	case	 MOUSE_LUP://左键抬起
		mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_RDOWN://单击右键按下
		mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, GetMessageExtraInfo());
		break;
	case	 MOUSE_RUP://右键抬起
		mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MDOWN://中间按下
		mouse_event(MOUSEEVENTF_MIDDLEDOWN, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MUP://中间抬起
		mouse_event(MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_LCLICK://左键单击
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_RCLICK://右键单击
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MCLICK://中间键单击
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case	 MOUSE_LDCLICK://左键双击
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		mouse_event(MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_LEFTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_RDCLICK://右键双击
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		mouse_event(MOUSEEVENTF_RIGHTDOWN | MOUSEEVENTF_RIGHTUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	case MOUSE_MDCLICK://鼠标中键双击
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		mouse_event(MOUSEEVENTF_MIDDLEDOWN | MOUSEEVENTF_MIDDLEUP, 0, 0, 0, GetMessageExtraInfo());
		break;
	default:
		std::cout << "未知鼠标操作" << std::endl;
		break;
	}

	return 0;
}

//键盘结构体
struct KeyBoard
{
	int  virtual_code;//虚拟码
	int key_status;//按下或者放开
};

//控制屏幕
int HandScreen(Packet* packet)
{
	CImage image;//获取本地屏幕,创建对象
	HDC hScreen = GetDC(nullptr);//拿到本机屏幕的上下文句柄
	int bit_width = GetDeviceCaps(hScreen, BITSPIXEL);//拿到屏幕的像素位宽
	//std::cout << "bit_width:" << bit_width << std::endl;
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);//开始DPi动态感知
	int sWidth = GetSystemMetrics(SM_CXSCREEN);//屏幕的宽
	int sHeight = GetSystemMetrics(SM_CYSCREEN);//屏幕的高
	//std::cout << "sWidth=" << sWidth << " " << "sHeight=" << sHeight << std::endl;
	image.Create(sWidth, sHeight, bit_width);//创建图片,
	HDC  hdc_image = image.GetDC();//从 image 这张图片中获取一个设备上下文 HDC，并保存到 hdc_image
	BitBlt(hdc_image, 0, 0, sWidth, sHeight, hScreen, 0, 0, SRCCOPY);//把屏幕的数据复制到image中
	image.ReleaseDC();//释放image 拿到的HDC
	ReleaseDC(NULL, hScreen);//释放屏幕的设备句柄
	//使用网络进行发送
	HGLOBAL hMen = GlobalAlloc(GMEM_MOVEABLE, 0);//在堆上申请内存，返回的是HGLOBAL，句柄。
	if (hMen == nullptr)
	{
		return -1;//检查是否成功
	}
	IStream* pStream = nullptr;//创建内存流
	HRESULT ret = CreateStreamOnHGlobal(hMen, true, &pStream);//用hMen创建一个IStream
	if (ret == S_OK)
	{
		image.Save(pStream, ::Gdiplus::ImageFormatPNG);//将文件保存到内存中
		LARGE_INTEGER lg = { 0 };
		pStream->Seek(lg, STREAM_SEEK_SET, nullptr);//把指针移回首位
		char* pdata = (char*)GlobalLock(hMen);
		int len = GlobalSize(hMen);
		Packet* screen_packet = PackPacket(CMD_SCREEN, pdata, len);
		//统计屏幕数据发给控制端的时间
		ULONGLONG send_start = GetTickCount64();
		//发送被控端的屏幕数据
		int send_ret = SendAll(screen_connected_socket, (char*)&screen_packet->header.magic, GetPacketLength(screen_packet));
		ULONGLONG send_end = GetTickCount64();
		if (send_ret == -1)
		{
			std::cout << "屏幕数据发送失败" << std::endl;
		}
		std::cout
			<< "SendAll耗时："
			<< send_end - send_start
			<< " ms，发送大小："
			<< GetPacketLength(screen_packet) / 1024
			<< " KB"
			<< std::endl;
		delete[]reinterpret_cast<char*>(screen_packet);
		screen_packet = nullptr;
		GlobalUnlock(hMen);//解锁内存，释放句柄
	}
	if (pStream != nullptr)
	{
		pStream->Release();//释放流指针
		pStream = nullptr;
	}
	return 0;
}

int HandKeyBoard(Packet* packet)
{
	if (packet->header.body_len != sizeof(KeyBoard))
	{
		std::cout << "键盘数据长度错误" << std::endl;
		return 0;
	}
	KeyBoard key_board{};
	memcpy(&key_board.virtual_code, packet->body, sizeof(KeyBoard));
	INPUT  input{ 0 };
	input.ki.wVk = key_board.virtual_code;//设置虚拟键码
	input.ki.wScan = 0;//设置键盘扫描码
	input.ki.time = 0;//键盘事件的时间戳
	input.ki.dwFlags = key_board.key_status;//键盘按下0；键盘松开
	input.ki.dwExtraInfo = 0;
	input.type = INPUT_KEYBOARD;//告诉 Windows：这是键盘输入
	int ret = SendInput(1, &input, sizeof(INPUT));//真正执行键盘模拟
	if (ret == 1)//函数返回的是成功插入系统输入队列的 INPUT 数量。
	{
		std::cout << "成功执行键盘事件" << std::endl;
	}
	else
	{
		std::cout << "键盘事件执行失败" << std::endl;
	}
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
		break;
	case CMD_MOUSE://鼠标
		ret = HandMouse(packet);
		break;
	case CMD_KEYBOARD://键盘
		ret = HandKeyBoard(packet);
		break;
	case CMD_TestConnected://测试连接
		ret = HandTestConnect(packet);
		break;
	default:
		break;
	}
	return ret;
}

//专门接收鼠标和键盘数据的线程
DWORD WINAPI ControlThreadProc(LPVOID lpThraedParamter)
{
	char* buffer = new char[RECV_BUFFER_SIZE];
	int index = 0;//buffer 里面目前有0个有效字节
	while (true)
	{
		// =====================================
		// 只从 control_connected_socket 接收
		// =====================================
		int recv_length = recv(control_connected_socket, buffer + index, RECV_BUFFER_SIZE - index, 0);
		if (recv_length <= 0)
		{
			std::cout << "控制通道断开" << std::endl;
			break;
		}
		index = index + recv_length;//buffer 里面目前有recv_length个有效字节
		while (index >= sizeof(PacketHeader))//判断buffer里面的字节数是不是比包头的大小要多
		{
			Packet* packet_mouse_keyboard = ParsePacket(buffer, index);//
			if (packet_mouse_keyboard == nullptr)
			{
				// 数据可能还没收完整,回到recv继续接数据
				break;
			}
			//当前这个完整 Packet 一共占用了 buffer 中的多少个字节
			int pack_length = GetPacketLength(packet_mouse_keyboard);
			// ==============================
		   // 控制socket只处理鼠标和键盘
		   // ==============================
			if (packet_mouse_keyboard->header.cmd == CMD_MOUSE)
			{
				HandMouse(packet_mouse_keyboard);
			}
			else if (packet_mouse_keyboard->header.cmd == CMD_KEYBOARD)
			{
				HandKeyBoard(packet_mouse_keyboard);
			}
			// buffer中上一个完整Packet的字节被处理了，更新这个index，这是现在buffer中剩余的字节数
			index = index - pack_length;
			if (index > 0)//如果还有剩余数据
			{
				memmove(buffer, buffer + pack_length, index);//移动到buffer的最前端来
			}
			delete[] reinterpret_cast<char*>(packet_mouse_keyboard);
			packet_mouse_keyboard = nullptr;
		}
	}
	delete[] buffer;
	buffer = nullptr;
	return 0;
}

//专门接收鼠标和键盘数据的线程
DWORD WINAPI ScreenThreadProc(LPVOID lpThraedParamter)
{
	char* buffer = new char[RECV_BUFFER_SIZE];
	int index = 0;//buffer 里面目前有0个有效字节
	while (true)
	{
		// =====================================
		// 只从 screen_connected_socket 接收
		// =====================================
		int recv_length = recv(screen_connected_socket, buffer + index, RECV_BUFFER_SIZE - index, 0);
		if (recv_length <= 0)
		{
			std::cout << "屏幕通道断开" << std::endl;
			break;
		}
		index = index + recv_length;//buffer 里面目前有recv_length个有效字节
		while (index >= sizeof(PacketHeader))//判断buffer里面的字节数是不是比包头的大小要多
		{
			Packet* packet_screen = ParsePacket(buffer, index);//
			if (packet_screen == nullptr)
			{
				break;// 数据可能还没收完整,回到recv继续接数据
			}
			//当前这个完整 Packet 一共占用了 buffer 中的多少个字节
			int pack_length = GetPacketLength(packet_screen);
			// ==============================
		   // 控制socket只处理屏幕
		   // ==============================
			if (packet_screen->header.cmd == CMD_SCREEN)
			{
				HandScreen(packet_screen);
			}
			// buffer中上一个完整Packet的字节被处理了，更新这个index，这是现在buffer中剩余的字节数
			index = index - pack_length;
			if (index > 0)//如果还有剩余数据
			{
				memmove(buffer, buffer + pack_length, index);//移动到buffer的最前端来
			}
			delete[] reinterpret_cast<char*>(packet_screen);
			packet_screen = nullptr;
		}
	}
	delete[] buffer;
	buffer = nullptr;
	return 0;
}

int main()
{
	SetConsoleCP(CP_UTF8);
	SetConsoleOutputCP(CP_UTF8);
	std::cout << "等待控制端连接..." << std::endl;
	// ===========================
	// 1. 创建两个监听socket
	// ===========================
	if (InitSocket() == 0)
	{
		std::cout << "启动网络初始化失败" << std::endl;
		return 0;
	}
	// ===========================
// 2. 等待控制通道连接
// ===========================
	std::cout << "等待控制通道连接..." << std::endl;
	SOCKADDR_IN  control_client_addr{};//连接进来的客户端地址信息
	int  control_addr_len = sizeof(control_client_addr);
	control_connected_socket = accept(control_listen_socket,
		reinterpret_cast<sockaddr*>(&control_client_addr), &control_addr_len);
	if (control_connected_socket == INVALID_SOCKET)// 判断 accept 是否成功
	{
		std::cout << "控制通道连接失败" << std::endl;
		//closesocket(server_socket);
		//WSACleanup();
		return 0;
	}
	std::cout << "控制通道连接成功" << std::endl;
	// ===========================
   // 3. 等待屏幕通道连接
   // ===========================
	std::cout << "等待屏幕通道连接..." << std::endl;
	SOCKADDR_IN  screen_client_addr{};//连接进来的客户端地址信息
	int  screen_addr_len = sizeof(screen_client_addr);
	screen_connected_socket = accept(screen_listen_socket,
		reinterpret_cast<sockaddr*>(&screen_client_addr), &screen_addr_len);
	if (screen_connected_socket == INVALID_SOCKET)// 判断 accept 是否成功
	{
		std::cout << "屏幕通道连接失败" << std::endl;
		//closesocket(server_socket);
		//WSACleanup();
		return 0;
	}
	std::cout << "屏幕通道连接成功" << std::endl;
	// ===========================
	// 4. 创建控制线程,返回线程句柄到control_thread
	// ===========================
	HANDLE control_thread = CreateThread(nullptr, 0, ControlThreadProc, nullptr, 0, nullptr);
	// ===========================
   // 5. 创建屏幕线程， 返回线程句柄到screen_thread
   // ===========================
	HANDLE screen_thread = CreateThread(nullptr, 0, ScreenThreadProc, nullptr, 0, nullptr);
	std::cout << "远程控制服务启动完成" << std::endl;
	// ===========================
	// 6. 等两个工作线程 threads
	//┌──────────────────┐
	//	│ threads[0]       │ → control_thread
	//	├──────────────────┤
	//	│ threads[1]       │ → screen_thread
	//	└──────────────────┘
	// ===========================
	HANDLE threads[2] = { control_thread , screen_thread };
	WaitForMultipleObjects(2, threads, TRUE, INFINITE);
	// ===========================
   // 7. 清理
   // ===========================
	CloseHandle(control_thread);
	CloseHandle(screen_thread);
	closesocket(control_connected_socket);
	closesocket(control_listen_socket);
	closesocket(screen_connected_socket);
	closesocket(screen_listen_socket);
	WSACleanup();
	return 0;
}