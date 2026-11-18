#include <iostream>
#include<string>
#include<Windows.h>
#include<atlimage.h>
#pragma comment(lib,"ws2_32.lib")
#define RECV_BUFFER_len 1024*1024*10 //从服务器接受的数据长度
SOCKET control_socket = INVALID_SOCKET;// 鼠标 + 键盘
SOCKADDR_IN control_serve_addr{};
SOCKET screen_socket = INVALID_SOCKET;// 屏幕
SOCKADDR_IN screen_serve_addr{};
#define CONTROL_PORT 9999
#define SCREEN_PORT 10000
#define WM_NEW_REMOTE_FRAME (WM_APP + 1)

CImage image;
HWND hwnd;
int g_mote_width = -1;
int g_mote_height = -1;

CRITICAL_SECTION g_image_lock;//初始化image的锁
CRITICAL_SECTION g_control_lock;//初始化send的锁

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

//枚举，定义远程控制程序中的命令类型
enum CMD
{
	CMD_SCREEN = 1,
	CMD_MOUSE = 2,
	CMD_KEYBOARD = 4,
	CMD_TestConnected = 3
};

//定义鼠标的枚举常量和结构体
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
//鼠标
struct  Mouse
{
	int action;//鼠标行为，左键按下，抬起，双击等等, 上面enmu定义的
	POINT ptXY;//鼠标的坐标, x,y 坐标
};
//键盘
struct KeyBoard
{
	int  virtual_code;//虚拟码
	int key_status;//按下或者放开
};

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
			if (len - start_head < sizeof(PacketHeader))
			{//当前的buffer长度-包头开始的位置，判断剩下的buffer长度能不能装下一个PacketHeader
				std::cout << "剩下的buffer长度不能装下一个PacketHeader" << std::endl;
				return nullptr;
			}
			//给包头进行赋值
			packet_header.magic = *(int*)(buffer + index);
			index += 4;
			packet_header.cmd = *(int*)(buffer + index);
			index += 4;
			packet_header.body_len = *(int*)(buffer + index);
			std::cout << "数据包的大小：" << packet_header.body_len << std::endl;
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

//初始化socket
int InitSocket()
{
	WSADATA wsadata;// 1. 初始化 Winsock
	int result = WSAStartup(MAKEWORD(2, 2), &wsadata);
	if (result != 0)
	{
		std::cout << "WSA初始化失败: " << result << std::endl;
		return 0;
	}

	// ===========================
  // 创建键鼠控制socket
  // ===========================
	control_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 创建键鼠 socket
	if (control_socket == INVALID_SOCKET)
	{
		std::cout << "创建键鼠socket失败，错误码：" << WSAGetLastError() << std::endl;
		return 0;
	}
	// ===========================
// 创建屏幕socket
// ===========================
	screen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);// 创建屏幕socket
	if (control_socket == INVALID_SOCKET)
	{
		std::cout << "创建屏幕socket失败，错误码：" << WSAGetLastError() << std::endl;
		return 0;
	}
	// ===========================
	// 两个socket连接的是同一台电脑，但端口不同
	// ===========================
	const char* remote_ip = "100.80.250.34";//笔记本Tailscale IPv4 的地址

	// 键鼠控制地址
	control_serve_addr.sin_family = AF_INET;
	control_serve_addr.sin_port = htons(CONTROL_PORT);
	control_serve_addr.sin_addr.s_addr = inet_addr(remote_ip);//本机是127.0.0.1， 笔记本的是192.168.1.4
	//直接走两台电脑的  Tailscale IPv4 ：笔记本是100.80.250.34

	// 屏幕地址
	screen_serve_addr.sin_family = AF_INET;
	screen_serve_addr.sin_port = htons(SCREEN_PORT);
	screen_serve_addr.sin_addr.s_addr = inet_addr(remote_ip);//本机是127.0.0.1， 笔记本的是192.168.1.4
	return 1;
}

//Windows 窗口的“消息处理函数”，也叫：窗口过程函数（Window Procedure） 这是回调函数
//Windows 在有窗口消息需要处理时，会主动调用它，并把消息内容作为参数传进来
LRESULT CALLBACK winProc(HWND  hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
//winProc 是 Windows 窗口的消息处理函数 1.窗口句柄, 2. 消息编号, 3. 消息附加参数 1,  4. 消息附加参数 2
{
	switch (msg)
	{
		//在已经存在的窗口客户区里，重新绘制当前的窗口图像。
	case WM_PAINT:
	{
		PAINTSTRUCT ps;//本次绘图的信息写到 PAINTSTRUCT 的地址中
		HDC hdc = BeginPaint(hwnd, &ps);// hdc就是图片最终要画到的绘图环境里,即设备上下文，  hwnd 窗口句柄, 表示要绘制的窗口
		// =========================
	   // UI线程要使用 image，先拿锁
	   // =========================
		EnterCriticalSection(&g_image_lock);
		if (!(image.IsNull()))
		{
			//这里需要做缩放，先拿到客户区域的大小
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			//远程图片的宽和高
			int remote_height = image.GetHeight();
			int  remote_width = image.GetWidth();
			//设置拉升模式  HALFTONE 高清
			int oldmode = SetStretchBltMode(hdc, HALFTONE);
			SetBrushOrgEx(hdc, 0, 0, nullptr);//把 hdc 这个绘图环境的“画刷原点”设置到 (0, 0)，并且不需要返回修改前的旧原点
			image.StretchBlt(//UI 主线程正在读取并绘制这个全局 image
				hdc,//画到当前窗口
				0, 0,
				client_width, client_height,//窗口客户区
				0, 0,
				remote_width, remote_height,//原始图片的窗口
				SRCCOPY);//直接把源图片复制到目标区域
			SetStretchBltMode(hdc, oldmode);//恢复原来的拉伸模式
		}
		// =========================
		// image已经画完了，释放锁
	   // =========================
		LeaveCriticalSection(&g_image_lock);
		EndPaint(hwnd, &ps);
		return 0;
	}
	case WM_NEW_REMOTE_FRAME:
	{
		// 新的一帧已经准备好了
		InvalidateRect(hwnd, nullptr, FALSE);
		// 立即处理WM_PAINT
		UpdateWindow(hwnd);
		return 0;
	}
	//鼠标移动
	case WM_MOUSEMOVE:
	{
		static ULONGLONG last_mouse_sned_time = 0;//记录上一次发送鼠标移动命令的时间,初始化为0
		ULONGLONG now = GetTickCount64();//获取现在的时间：
		if (now - last_mouse_sned_time < 20)
		{
			return 0;
		}
		last_mouse_sned_time = now;//记录这次发送时间
		//拿到当前的鼠标位置
		int xPos = LOWORD(lParam);//lParam 中取出低 16 位为X坐标
		int yPos = HIWORD(lParam);//lParam 中取出高 16 位为Y坐标
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int client_width = client_rect.right - client_rect.left;
		int client_height = client_rect.bottom - client_rect.top;
		if (g_mote_width != -1 && g_mote_height != -1)
		{
			int rxPos = xPos * g_mote_width / client_width;
			int ryPos = yPos * g_mote_height / client_height;
			//发送数据
			Mouse mouse;
			mouse.action = MOUSE_MOVE;
			mouse.ptXY.x = rxPos;
			mouse.ptXY.y = ryPos;
			Packet* packet_mouse = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(Mouse));
			EnterCriticalSection(&g_control_lock);//进入临界区，也就是加锁。
			send(control_socket, reinterpret_cast<char*>(packet_mouse), GetPacketLength(packet_mouse), 0);
			LeaveCriticalSection(&g_control_lock);//离开临界区，也就是解锁
			//OutputDebugString("发送鼠标移动请求");
			delete[]reinterpret_cast<char*>(packet_mouse);
			packet_mouse = nullptr;
		}
	}
	break;
	//鼠标左键按下
	case WM_LBUTTONDOWN:
	{
		//拿到当前的鼠标位置
		int xPos = LOWORD(lParam);//lParam 中取出低 16 位为X坐标
		int yPos = HIWORD(lParam);//lParam 中取出高 16 位为Y坐标
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);
		int client_width = client_rect.right - client_rect.left;
		int client_height = client_rect.bottom - client_rect.top;
		if (g_mote_width != -1 && g_mote_height != -1)
		{
			int rxPos = xPos * g_mote_width / client_width;
			int ryPos = yPos * g_mote_height / client_height;
			//发送数据
			Mouse mouse;
			mouse.action = MOUSE_LDOWN;
			mouse.ptXY.x = rxPos;
			mouse.ptXY.y = ryPos;
			Packet* packet_mouse = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(Mouse));
			EnterCriticalSection(&g_control_lock);
			send(control_socket, reinterpret_cast<char*>(packet_mouse), GetPacketLength(packet_mouse), 0);
			LeaveCriticalSection(&g_control_lock);
			delete[]reinterpret_cast<char*>(packet_mouse);
			packet_mouse = nullptr;
		}
	}
	break;
	//鼠标左键抬起
	case WM_LBUTTONUP:
	{
		//拿到当前的鼠标位置
		int xPos = LOWORD(lParam);//lParam 中取出低 16 位为X坐标
		int yPos = HIWORD(lParam);//lParam 中取出高 16 位为Y坐标
		RECT client_rect;
		GetClientRect(hwnd, &client_rect);//
		int client_width = client_rect.right - client_rect.left;
		int client_height = client_rect.bottom - client_rect.top;
		if (g_mote_width != -1 && g_mote_height != -1)
		{
			int rxPos = xPos * g_mote_width / client_width;
			int ryPos = yPos * g_mote_height / client_height;
			//发送数据
			Mouse mouse;
			mouse.action = MOUSE_LUP;
			mouse.ptXY.x = rxPos;
			mouse.ptXY.y = ryPos;
			Packet* packet_mouse = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(Mouse));
			EnterCriticalSection(&g_control_lock);
			send(control_socket, reinterpret_cast<char*>(packet_mouse), GetPacketLength(packet_mouse), 0);
			LeaveCriticalSection(&g_control_lock);
			delete[]reinterpret_cast<char*>(packet_mouse);
			packet_mouse = nullptr;
		}
	}
	break;
	//鼠标右键按下
	case WM_RBUTTONDOWN:
	{
		{
			//拿到当前的鼠标位置
			int xPos = LOWORD(lParam);//lParam 中取出低 16 位为X坐标
			int yPos = HIWORD(lParam);//lParam 中取出高 16 位为Y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect); //获取 hwnd 这个窗口的“客户区”大小，
			//并把结果写进 client_rect 这个 RECT 结构体
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_mote_width != -1 && g_mote_height != -1)
			{
				int rxPos = xPos * g_mote_width / client_width;
				int ryPos = yPos * g_mote_height / client_height;
				//发送数据
				Mouse mouse;
				mouse.action = MOUSE_RDOWN;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet_mouse = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(Mouse));
				EnterCriticalSection(&g_control_lock);
				send(control_socket, reinterpret_cast<char*>(packet_mouse), GetPacketLength(packet_mouse), 0);
				LeaveCriticalSection(&g_control_lock);
				delete[]reinterpret_cast<char*>(packet_mouse);
				packet_mouse = nullptr;
			}
		}
	}
	break;
	//鼠标右键抬起
	case WM_RBUTTONUP:
	{
		{
			//拿到当前的鼠标位置
			int xPos = LOWORD(lParam);//lParam 中取出低 16 位为X坐标
			int yPos = HIWORD(lParam);//lParam 中取出高 16 位为Y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_mote_width != -1 && g_mote_height != -1)
			{
				int rxPos = xPos * g_mote_width / client_width;
				int ryPos = yPos * g_mote_height / client_height;
				//发送数据
				Mouse mouse;
				mouse.action = MOUSE_RUP;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet_mouse = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(Mouse));
				EnterCriticalSection(&g_control_lock);
				send(control_socket, reinterpret_cast<char*>(packet_mouse), GetPacketLength(packet_mouse), 0);
				LeaveCriticalSection(&g_control_lock);
				delete[]reinterpret_cast<char*>(packet_mouse);
				packet_mouse = nullptr;
			}
		}
	}
	break;
	//鼠标左键双击
	case WM_LBUTTONDBLCLK:
	{
		{
			//拿到当前的鼠标位置
			int xPos = LOWORD(lParam);//lParam 中取出低 16 位为X坐标
			int yPos = HIWORD(lParam);//lParam 中取出高 16 位为Y坐标
			RECT client_rect;
			GetClientRect(hwnd, &client_rect);
			int client_width = client_rect.right - client_rect.left;
			int client_height = client_rect.bottom - client_rect.top;
			if (g_mote_width != -1 && g_mote_height != -1)
			{
				int rxPos = xPos * g_mote_width / client_width;
				int ryPos = yPos * g_mote_height / client_height;
				//发送数据
				Mouse mouse;
				mouse.action = MOUSE_LDCLICK;
				mouse.ptXY.x = rxPos;
				mouse.ptXY.y = ryPos;
				Packet* packet_mouse = PackPacket(CMD_MOUSE, (char*)&mouse.action, sizeof(Mouse));
				EnterCriticalSection(&g_control_lock);
				send(control_socket, reinterpret_cast<char*>(packet_mouse), GetPacketLength(packet_mouse), 0);
				LeaveCriticalSection(&g_control_lock);
				delete[]reinterpret_cast<char*>(packet_mouse);
				packet_mouse = nullptr;
			}
		}
	}
	break;
	//键盘信息
	case WM_KEYDOWN:

	case WM_SYSKEYUP:
	{
		//虚拟键码
		KeyBoard keyboard;
		keyboard.virtual_code = wParam;
		keyboard.key_status = 0;
		Packet* packet_keyboard = PackPacket(CMD_KEYBOARD, (char*)&keyboard.virtual_code, sizeof(KeyBoard));
		send(control_socket, reinterpret_cast<char*>(packet_keyboard), GetPacketLength(packet_keyboard), 0);
		delete[]reinterpret_cast<char*>(packet_keyboard);
		packet_keyboard = nullptr;
	}
	break;
	//结束窗口
	case WM_DESTROY:
	{
		PostQuitMessage(0);//向当前线程的消息队列中放入一个 WM_QUIT 消息，返回0然后结束while消息循环
		return 0;
	}

	default://前面没处理就把这个windows消息交给 Windows 默认窗口过程
		return DefWindowProc(hwnd, msg, wParam, lParam);
		break;
	}
	return 0;
}

// 屏幕接收线程的入口函数 ，不断绘制下一张屏幕
DWORD WINAPI SendScreenCallBAck(LPVOID lpThreadParamter)//线程参数：接收 CreateThread 第4个参数传进来的数据
{
	char* recv_buffer = new char[RECV_BUFFER_len];
	int index = 0;
	bool running = true;
	while (running)//CreateThread() 创建出来的新线程的“入口函数”,新线程从这个开始执行
	{
		//加一个计时器，这是开始发动请求的时间
		ULONGLONG frame_start = GetTickCount64();
		// ==========================================
		// 1. 请求被控端发送一张屏幕截图
		// ==========================================
		Packet* screen_pack = PackPacket(CMD_SCREEN, NULL, 0);
		int sent_len = send(screen_socket, (char*)&(screen_pack->header.magic), GetPacketLength(screen_pack), 0);//给被控端发送屏幕请求
		if (sent_len == SOCKET_ERROR)
		{
			OutputDebugString("发送屏幕请求失败\n");
			break;
		}
		if (sent_len > 0)
		{
			OutputDebugString("给被控端成功发送屏幕请求");
		}
		delete[] reinterpret_cast<char*>(screen_pack);
		screen_pack = nullptr;
		// ==========================================
		// 2. 一直收，直到拿到一个完整截图Packet
		// ==========================================
		bool get_whole_screen = false;
		while (!get_whole_screen)
		{
			if (index >= RECV_BUFFER_len)
			{
				OutputDebugStringA("接收缓冲区已满\n");
				running = false;
				break;
			}
			ULONGLONG recv_start = GetTickCount64();
			int  len = recv(screen_socket, recv_buffer + index, RECV_BUFFER_len - index, 0);//接受被控段返回的图片数据
			ULONGLONG recv_end = GetTickCount64();
			if (len > 0)
			{
				index = index + len;
				//看看屏幕数据包有多大
				char debug_text[256];
				sprintf_s(
					debug_text,
					"SCREEN recv等待=%llu ms，本次=%d KB，累计=%d KB\n",
					recv_end - recv_start,
					len / 1024,
					index / 1024
				);
				OutputDebugStringA(debug_text);
				// ==================================
			   // 3. 尝试从buffer中解析完整Packet
			   // ==================================
				while (true)
				{
					//开始解析数据
					Packet* recv_packet = ParsePacket(recv_buffer, index);
					if (recv_packet == nullptr)
					{
						break;
					}
					//这是接受完整数据的时间
					ULONGLONG receive_done = GetTickCount64();
					ULONGLONG network_time = receive_done - frame_start;//一个完整的请求到接受的时间
					int packet_length = GetPacketLength(recv_packet);
					// ==============================
				   // 4. 处理SCREEN响应
				   // ==============================
					if (recv_packet->header.cmd == CMD_SCREEN)//成功接受图片，然后进行绘制
					{
						//创建一个可移动的 HGLOBAL 内存对象
						HGLOBAL hMen = GlobalAlloc(GMEM_MOVEABLE, 0);
						if (hMen != nullptr)
						{
							IStream* pStream = nullptr;//创建内存流
							HRESULT ret = CreateStreamOnHGlobal(hMen, true, &pStream);//把 hMen交给pStream进行管理
							if (ret == S_OK)
							{
								//将body中的数据写入 pStream 指向的内存流
								ULONG length = 0;
								pStream->Write(recv_packet->body, recv_packet->header.body_len, &length);
								//把pStream指针移回首位
								LARGE_INTEGER lg = { 0 };
								pStream->Seek(lg, STREAM_SEEK_SET, nullptr);
								// =============================
								// 先在锁外面解码图片
								// =============================

								CImage new_image;
								// 记录开始解码的时间
								ULONGLONG decode_start = GetTickCount64();
								// 真正解码 PNG / JPEG的地方
								HRESULT load_result = new_image.Load(pStream);
								// 记录解码完成的时间
								ULONGLONG decode_end = GetTickCount64();
								// ============================================
								// 解码成功以后，再快速替换全局 image
								// ============================================
								if (SUCCEEDED(load_result))
								{
									// 从临时 new_image 中取出 HBITMAP
									HBITMAP new_bitmap = new_image.Detach();
									// ==========================
									// 现在才需要 image 的锁
									// ==========================
									// 删除上一帧
									EnterCriticalSection(&g_image_lock);
									if (!image.IsNull())
									{
										image.Destroy();
									}
									image.Attach(new_bitmap);// 把新的一帧交给全局 image
									// 保存远程屏幕分辨率
									g_mote_width = image.GetWidth();
									g_mote_height = image.GetHeight();
									LeaveCriticalSection(&g_image_lock);
									// ==========================
									// 打印解码耗时
									// ==========================
									char debug_text[256];
									sprintf_s(
										debug_text,
										"client图片解码耗时 = %llu ms\n",
										decode_end - decode_start
									);
									OutputDebugStringA(debug_text);
									PostMessage(hwnd, WM_NEW_REMOTE_FRAME, 0, 0);//通知 Windows：这个窗口区域“无效了”，需要重新绘制
									//UpdateWindow(hwnd);//如果窗口有无效区域，就立即触发一次重绘,要求现在就处理这个重画
								}
								//释放接口对象 IStream 的引用
								pStream->Release();
								pStream = nullptr;
							}
						}
						else
						{
							GlobalFree(hMen);
						}
						get_whole_screen = true;
					}
					// ==================================
				  // 5. 从buffer中去掉已经处理的Packet
				  // ==================================
					index = index - packet_length;
					if (index > 0)
					{
						memmove(recv_buffer, recv_buffer + packet_length, index);
					}
					// ==================================
					// 6. 释放ParsePacket创建的Packet
					// ==================================
					delete[] reinterpret_cast<char*>(recv_packet);
					recv_packet = nullptr;
					if (get_whole_screen)
					{
						break;
					}
				}
			}
			else if (len == 0)
			{
				OutputDebugStringA("被控端关闭连接\n");
				running = false;
				break;
			}
			else
			{
				OutputDebugStringA("接收截图失败\n");
				running = false;
				break;
			}
		}
	}
	delete[] recv_buffer;
	recv_buffer = nullptr;
	return 0;
}

//创建窗口
int InitWindow(HINSTANCE hInstance, int nCmdShow)
{
	WNDCLASS ws = {};//第一步：注册窗口类
	LPCSTR CLASS_NAME = "MainWindow";
	ws.lpfnWndProc = winProc;//消息处理函数
	ws.hInstance = hInstance;//程序实例
	ws.lpszClassName = CLASS_NAME;//给这种窗口类型起名字
	ws.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); //系统默认窗口背景颜色
	ws.hCursor = LoadCursor(nullptr, IDC_ARROW);//设置鼠标
	ws.hIcon = LoadIconA(nullptr, IDI_APPLICATION);//设置窗口图标
	ws.style = CS_HREDRAW | CS_VREDRAW;//窗口宽度或者高度变化时，让 Windows 重新绘制窗口
	if (!RegisterClass(&ws))//将模板注册给 Windows
	{
		MessageBoxA(NULL, "窗口注册失败", "错误", MB_OK | MB_ICONERROR);
		return 0;
	}
	hwnd = CreateWindow(CLASS_NAME, "远程控制",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
		600, 400,
		NULL, NULL,
		hInstance, NULL);//第二步：创建窗口
	if (hwnd == nullptr)
	{
		MessageBoxA(NULL, "窗口创建失败", "错误", MB_OK | MB_ICONERROR);
		return 0;
	}
	ShowWindow(hwnd, nCmdShow);//第三步: 显示窗口
	UpdateWindow(hwnd);//第四步：更新窗口
	return 1;
}

//窗口入口函数
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPreventInstance, PSTR pCmdLine, int nCmdShow)
//1.当前程序实例的句柄 2.//前一个程序实例的句柄
//  3. 传递进来的命令行参数字符串4.主窗口第一次显示时应该是什么状态
{
	InitializeCriticalSection(&g_image_lock);//调用 图像 的锁
	InitializeCriticalSection(&g_control_lock);//调用键鼠控制的锁
	if (InitWindow(hInstance, nCmdShow) == 0)//初始化窗口
	{
		return 0;
	}
	if (InitSocket() == 0)//初始化网络
	{
		return 0;
	}
	// =============================
// 1. 连接控制通道9999
// =============================
	if (connect(control_socket, reinterpret_cast<sockaddr*>(&control_serve_addr), sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		OutputDebugString("控制通道连接失败\n");
		return 0;
	}
	OutputDebugString("控制通道连接成功\n");
	// =============================
// 2. 连接屏幕通道10000
// =============================
	if (connect(screen_socket, reinterpret_cast<sockaddr*>(&screen_serve_addr), sizeof(SOCKADDR_IN)) == SOCKET_ERROR)
	{
		OutputDebugString("屏幕通道连接失败\n");
		return 0;
	}
	OutputDebugString("屏幕通道连接成功\n");

	//新开一个新的线程送请求屏幕的数据和解析
	unsigned long send_screen_thread_id = 0;
	// 1. 线程安全属性：NULL 表示使用默认安全属性   2. 线程栈大小：0 表示使用系统默认栈大小
	//  3. 线程入口函数：新线程创建后，从这个函数开始执行
	// 4. 传给线程入口函数的参数：NULL 表示目前不给 SendScreenCallBAck 传任何数据
	// 5. 线程创建标志：0 表示线程创建成功后立即开始运行   6. 用来接收新线程的线程 ID
	HANDLE handle_send_screen = CreateThread(NULL, 0, SendScreenCallBAck, NULL, 0, &send_screen_thread_id);
	OutputDebugString("连接被控端成功");
	MSG msg = {};//把整个结构体初始化为零值
	while (GetMessageA(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);//翻译消息
		DispatchMessage(&msg);//分发消息给winProc 真正处理
	}
}