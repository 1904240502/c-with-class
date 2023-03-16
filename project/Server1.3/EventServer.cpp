#include "EventServer.h"

EventServer::EventServer(Event* pEvent,SOCKET sock):_sock(sock)
{
	memset(_BUF, 0, sizeof(_BUF));
	_pThread = nullptr;
	_pEvent = pEvent;
}

EventServer::~EventServer()
{
	Close();
	_sock = INVALID_SOCKET;
	delete _pThread;
	std::cout << "服务端处理退出！" << std::endl;
}

void EventServer::start()
{
	//开启接收客户消息
	_pThread = new std::thread(std::mem_fn(&EventServer::Run), this);
	//开启发送任务给客户
	_Tasks.Start();
}

void EventServer::addClientToSet(Client* client)
{
	std::lock_guard<std::mutex> lock(_mutex);
	_ClientSet.push_back(client);
}

int EventServer::GetClientNum()
{
	return (int)(_Clients.size() + _ClientSet.size());
}

std::thread* EventServer::GetThread()
{
	return _pThread;
}

void EventServer::changeSocket()
{
	_sock = INVALID_SOCKET;
}

void EventServer::addSendTask(Client* pClient, DataHeader* header)
{
	_Tasks.addTask(new TaskSend(pClient, header));
}

void EventServer::Run()
{
	fd_set cache{};
	FD_ZERO(&cache);

	bool isChange=false;
	SOCKET maxfd = 0;
	while (isCon())
	{
		//缓冲客户集->客户集
		if (addToClients())
			isChange = true;
		//没有客户
		if (_Clients.empty())
		{
			//避免客户空处理
			std::chrono::milliseconds t(1);
			std::this_thread::sleep_for(t);
			continue;
		}

		//创建文件描述符集
		fd_set fdRead{};
		//置零
		FD_ZERO(&fdRead);
		if (isChange)
		{
			maxfd = _Clients.begin()->first;
			for (auto& val : _Clients)
			{
				FD_SET(val.first, &fdRead);
				if (val.first > maxfd)
					maxfd = val.first;
			}
			memcpy(&cache, &fdRead, sizeof(fd_set));
			isChange = false;
		}
		else {
			memcpy(&fdRead, &cache, sizeof(fd_set));
		}

		int ret = select((int)maxfd + 1, &fdRead, nullptr, nullptr, nullptr);
		if (ret < 0)
		{
			std::cout << "消息 select 结束！" << std::endl;
			Close();
			return;
		}
		else if (ret == 0)
		{
			continue;
		}

		//处理监听事件
		vector<Client*> temp;
		for (auto val : _Clients)
		{
			if (FD_ISSET(val.first, &fdRead))
			{
				if (!RecvInfo(val.second))
				{
					//在server中删除客户
					if (_pEvent)
						_pEvent->onLeave(val.second);

					temp.push_back(val.second);
					isChange = true;
				}
			}
		}

		for (auto val : temp)
		{
			_Clients.erase(val->GetSocket());
			closesocket(val->GetSocket());
			delete val;
		}
	}
	//线程退出
	
	if (!_Clients.empty())
	{
		/*
	    * 接收完整消息后再推出
	    */
	}
	return;
}

bool EventServer::isCon()
{
	return INVALID_SOCKET != _sock;
}

void EventServer::Close()
{
	if (isCon())
	{
		//清理关闭客户连接
		for (int i=0;i<_ClientSet.size();i++)
		{
			closesocket(_ClientSet[i]->GetSocket());
			delete _ClientSet[i];
		}
		_ClientSet.clear();
		for (int i = 0; i < _Clients.size(); i++)
		{
			closesocket(_Clients[i]->GetSocket());
			delete _Clients[i];
		}
		_Clients.clear();
	}
}

bool EventServer::addToClients()
{
	if (_ClientSet.empty())
		return false;
	std::lock_guard<std::mutex> lock(_mutex);
	for (auto val : _ClientSet)
		_Clients[val->GetSocket()] = val;
	_ClientSet.clear();
	return true;
}

bool EventServer::RecvInfo(Client* client)
{
	//获得数据长度
	auto len = sizeof(DataHeader);

	//接收客户数据
	auto rlen = recv(client->GetSocket(), _BUF, sizeof(_BUF), 0);
	if (rlen < 0)
	{
		//std::cout << "客户端: " << client->GetSocket() << "已退出！" << std::endl;
		return false;
	}
	memcpy(client->GetRecvBufs() + client->GetRecvPos(), _BUF, rlen);
	client->SetRecvPos(client->GetRecvPos() + rlen);

	//接收数据
	while (client->GetRecvPos() >= len)
	{
		//获得数据长度
		DataHeader* header = (DataHeader*)client->GetRecvBufs();
		//缓冲中已经有完整的包
		if (client->GetRecvPos() >= header->dataLength)
		{
			int nlen = header->dataLength;
			//处理消息
			onMsg(client, header);
			//从缓冲中取出包
			memcpy(client->GetRecvBufs(), client->GetRecvBufs() + nlen, client->GetRecvPos() - nlen);
			client->SetRecvPos(client->GetRecvPos() - nlen);
		}
		else
			break;
	}

	return true;
}

void EventServer::onMsg(Client * pClient, DataHeader* header)
{	
	_pEvent->onMsg(this,pClient,header);
	//_pEvent->onMsg(pClient,header);
}


