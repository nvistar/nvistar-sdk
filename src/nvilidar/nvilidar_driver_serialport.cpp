#include "nvilidar_driver_serialport.h"
#include "serial/nvilidar_serial.h"
#include <list>
#include <string>
#include "myconsole.h"
#include "mytimer.h"
#include "mystring.h"

namespace nvilidar
{
	//构造函数
	LidarDriverSerialport::LidarDriverSerialport(Nvilidar_UserConfigTypeDef cfg)
	{
		lidar_state.m_SerialOpen = false;       //默认串口关闭
		lidar_state.m_Scanning = false;         //默认扫描接口关闭

		lidar_cfg = cfg;                   //配置参数生效
	}

	//析构函数
	LidarDriverSerialport::~LidarDriverSerialport()
	{
		LidarDisconnect();
	}

	//雷达是否连接
	bool LidarDriverSerialport::LidarIsConnected()
	{
		if(lidar_state.m_SerialOpen)
		{
			return true;
		}
		return false;
	}

	//初始化 获取参数等信息 
	bool LidarDriverSerialport::LidarInit()
	{
		Nvilidar_StoreConfigTypeDef store_para_read;		//读出的参数  

		//判断串口参数是否合法  
		if ((lidar_cfg.serialport_name.length() == 0) || (lidar_cfg.serialport_baud == 0))
		{
			return false;		//串口信息不合法 
		}

		//开始连接串口 
		if (!LidarConnect(lidar_cfg.serialport_name, lidar_cfg.serialport_baud))
		{
			nvilidar::console.error("Error initializing NVILIDAR scanner.");
		}

		//发送停止命令 
		StopScan();
		//sleep 
		delayMS(300);
		
		//获取雷达信息 
		if (false == GetDeviceInfo(lidar_cfg.deviceInfo))
		{
			nvilidar::console.warning("Error initializing NVILIDAR scanner.Failed to get Lidar Device Info.");
			return false;
		}
		nvilidar::console.show("\nlidar device info:");
		nvilidar::console.show("lidar name:%s", lidar_cfg.deviceInfo.m_ProductName.c_str());
		nvilidar::console.show("lidar soft version:%s", lidar_cfg.deviceInfo.m_SoftVer.c_str());
		nvilidar::console.show("lidar hard version:%s", lidar_cfg.deviceInfo.m_HardVer.c_str());
		nvilidar::console.show("lidar serialnumber:%s", lidar_cfg.deviceInfo.m_SerialNum.c_str());

		//获取雷达配置参数
		if (false == GetLidarCfg(store_para_read))
		{
			nvilidar::console.warning("Error initializing NVILIDAR scanner.Failed to get Lidar Config Info.");
			return false;
		}

#if 1
		bool isNeedSetPara = false;
		bool isSetOK = true;
		//判断参数是否一致 不一致则重置 
		if (lidar_cfg.storePara.samplingRate != store_para_read.samplingRate)		//采样率 
		{
			isNeedSetPara = true;
			if (!SetSamplingRate(lidar_cfg.storePara.samplingRate, store_para_read.samplingRate))
			{
				isSetOK = false;
			}
		}
		if (lidar_cfg.storePara.aimSpeed != store_para_read.aimSpeed)
		{
			isNeedSetPara = true;
			if (!SetScanMotorSpeed(lidar_cfg.storePara.aimSpeed, store_para_read.aimSpeed))
			{
				isSetOK = false;
			}
		}
		if (lidar_cfg.storePara.isHasSensitive != store_para_read.isHasSensitive)
		{
			isNeedSetPara = true;
			if(!SetIntensities(lidar_cfg.storePara.isHasSensitive))
			{
				isSetOK = false;
			}
			store_para_read.isHasSensitive = lidar_cfg.storePara.isHasSensitive;
		}
		if (lidar_cfg.storePara.tailingLevel != store_para_read.tailingLevel)
		{
			isNeedSetPara = true;
			if (!SetTrailingLevel(lidar_cfg.storePara.tailingLevel, store_para_read.tailingLevel))
			{
				isSetOK = false;
			}
		}
		if (isNeedSetPara)
		{
			if (isSetOK)
			{
				nvilidar::console.show("NVILIDAR set para OK!");
			}
			else
			{
				nvilidar::console.warning("NVILIDAR set para Fail!");
			}
		}
#endif

		//打印配置信息
		nvilidar::console.show("\nlidar config info:");
		nvilidar::console.show("lidar samplerate :%d", store_para_read.samplingRate);
		nvilidar::console.show("lidar frequency :%d.%02d", store_para_read.aimSpeed/100, store_para_read.aimSpeed%100);
		nvilidar::console.show("lidar sesitive :%s", store_para_read.isHasSensitive ? "yes" : "no");
		nvilidar::console.show("lidar tailling filter level :%d", store_para_read.tailingLevel);

		//sleep 
		//delayMS(5);

		return true;
	}

	//启动雷达  
	bool LidarDriverSerialport::LidarStartScan()
	{
		//启动雷达  
		if (!StartScan())
		{
			StopScan();

			nvilidar::console.error("[CNviLidar] Failed to start scan");

			return false;
		}

		//包数为0了 
		m_run_circles = 0;



		//启动成功
		nvilidar::console.message("[NVILIDAR INFO] Now NVILIDAR is scanning ......");

		return true;
	}

	//启动雷达  
	bool LidarDriverSerialport::LidarStopScan()
	{
		//停止雷达输出 
		StopScan();

		//包数为0了 
		m_run_circles = 0;

		return true;
	}

	bool LidarDriverSerialport::LidarCloseHandle()
	{
		LidarDisconnect();
		return true;
	}

	//---------------------------------------私有类及接口---------------------------------

	//启动雷达
	bool LidarDriverSerialport::StartScan()
	{
		//串口有没有开
		if (!lidar_state.m_SerialOpen)
		{
			return false;
		}
		//是否正在运行
		if (lidar_state.m_Scanning)
		{
			return true;
		}

		//first circle false
		m_first_circle_finish = false;

		//发送数据
		if (!SendCommand(NVILIDAR_CMD_SCAN))
		{
			return false;
		}

		lidar_state.m_Scanning = true;

		return true;
	}

	//雷达停止
	bool  LidarDriverSerialport::StopScan()
	{
		//扫描标记清0
		lidar_state.m_Scanning = false;

		//清除数据  
		FlushSerial();

		//发送数据
		SendCommand(NVILIDAR_CMD_STOP);

		return true;
	}

	//启动雷达串口
	bool LidarDriverSerialport::LidarConnect(std::string portname, uint32_t baud)
	{
		serialport.serialInit(portname,921600);
		serialport.serialOpen();
		
		if (serialport.isSerialOpen())
		{
			lidar_state.m_SerialOpen = true;

			delayMS(100);
			createThread();		//创建线程 接收数据 

			return true;
		}
		else
		{
			lidar_state.m_SerialOpen = false;	

			return false;
		}
	}

	//关闭雷达串口接口 
	void LidarDriverSerialport::LidarDisconnect()
	{
		//closeThread();		//关闭线程 
		serialport.serialClose();
	}

	//发送串口
	bool LidarDriverSerialport::SendSerial(const uint8_t *data, size_t size)
	{
		if (!lidar_state.m_SerialOpen)
		{
			return false;
		}

		if (data == NULL || size == 0)
		{
			return false;
		}

		
		//写数据 直到写完为止  
		size_t r;
		while (size) 
		{
			r = serialport.serialWriteData(data,size);
			if (r < 1) 
			{
				return false;
			}

			size -= r;
			data += r;
		}

		return true;
	}

	//刷新串口数据 
	void LidarDriverSerialport::FlushSerial()
	{
		if (!lidar_state.m_SerialOpen)
		{
			return;
		}

		serialport.serialFlush();
	}

	//雷达发送数据
	bool LidarDriverSerialport::SendCommand(uint8_t cmd, const void *payload, uint16_t payloadsize)
	{
		uint8_t pkt_header[sizeof(Nvilidar_ProtocolHeader)];
		Nvilidar_ProtocolHeader *header = reinterpret_cast<Nvilidar_ProtocolHeader *>(pkt_header);
		uint8_t checksum = 0;
		uint8_t pkt_tail = NVILIDAR_END_CMD;     //包尾

		//串口未打开 返回失败
		if (!lidar_state.m_SerialOpen)
		{
			return false;
		}

		if (payloadsize && payload)   //起始字节  根据有没有内容  来看是长命令字还是短命令字
		{
			//长命令
			header->startByte = NVILIDAR_START_BYTE_LONG_CMD;
			header->cmd = cmd;
			header->length = payloadsize;

			//计算校验值
			for (size_t pos = 0; pos < payloadsize; ++pos)
			{
				checksum ^= ((uint8_t *)payload)[pos];
			}

			uint16_t sizebyte = (uint8_t)(payloadsize);

			//开始进行发送
			SendSerial(pkt_header, 4);       //包头 长度信息
			SendSerial((const uint8_t *)payload, sizebyte);   //发送数据信息
			SendSerial(&checksum, 1);         //校验值
			SendSerial(&pkt_tail, 1);         //包尾
		}
		else
		{
			//短命令
			header->startByte = NVILIDAR_START_BYTE_SHORT_CMD;
			header->cmd = cmd;
			SendSerial(pkt_header, 2);
		}

		return true;
	}

	//普通数据解包 
	void LidarDriverSerialport::NormalDataUnpack(uint8_t *buf, uint16_t len)
	{
		static uint16_t  recvPos = 0;											//当前接到的位置信息
		static uint8_t   crc = 0;												//CRC校验值 
		static Nvilidar_Protocol_NormalResponseData		normalResponseData;				//常规数据应答  

		for (int j = 0; j < len; j++)
		{
			uint8_t byte = buf[j];

			switch (recvPos)
			{
				case 0:		//第一个字节  
				{
					if (byte == NVILIDAR_START_BYTE_LONG_CMD)
					{
						recvPos++;
						break;
					}
					else
					{
						break;
					}
				}
				case 1:		//第2个字节  
				{
					normalResponseData.cmd = byte;
					recvPos++;
					break;
				}
				case 2:		//第3个字节  
				{
					normalResponseData.length = byte;
					recvPos++;
					break;
				}
				case 3:		//第4个字节  
				{
					normalResponseData.length += byte * 256;
					recvPos++;
					break;
				}
				default:	//第5个及后续所有字节 
				{
					if (recvPos < normalResponseData.length + sizeof(Nvilidar_ProtocolHeader))			//中间有效数据  
					{
						crc ^= byte;
						normalResponseData.dataInfo[recvPos - sizeof(Nvilidar_ProtocolHeader)] = byte;

						recvPos++;
					}
					else if (recvPos == normalResponseData.length + sizeof(Nvilidar_ProtocolHeader))	//校验  
					{
						if (byte != crc)
						{
							recvPos = 0;
							break;
						}

						recvPos++;
					}
					else if (recvPos == normalResponseData.length + sizeof(Nvilidar_ProtocolHeader) + 1)
					{
						if (byte != NVILIDAR_END_CMD)
						{
							normalResponseData.length = 0;
							normalResponseData.cmd = 0;
							crc = 0;
							recvPos = 0;
							break;
						}

						//调用协议解析接口 
						NormalDataAnalysis(normalResponseData);

						//指针回到0位 
						normalResponseData.length = 0;
						normalResponseData.cmd = 0;
						crc = 0;
						recvPos = 0;
					}

					break;
				}
			}
		}
	}

	//协议解析  
	void LidarDriverSerialport::NormalDataAnalysis(Nvilidar_Protocol_NormalResponseData data)
	{
		switch (data.cmd)
		{
			case NVILIDAR_CMD_GET_DEVICE_INFO:		//获取设备信息 
			{
				if (data.length != sizeof(recv_info.lidar_device_info))
				{
					break;
				}

				memcpy((char *)(&recv_info.lidar_device_info), data.dataInfo, data.length);
				recv_info.recvFinishFlag = true;		//接收成功 

				//设置event失效 
				setNormalResponseUnlock();				//解锁 

				break;
			}
			case NVILIDAR_CMD_GET_LIDAR_CFG:		//获取雷达部分参数信息 
			{
				if (data.length != sizeof(recv_info.lidar_get_para))
				{
					break;
				}

				memcpy((char *)(&recv_info.lidar_get_para), data.dataInfo, data.length);
				recv_info.recvFinishFlag = true;		//接收成功

				//设置event失效 
				setNormalResponseUnlock();				//解锁 

				break;
			}
			case NVILIDAR_CMD_SET_HAVE_INTENSITIES:	//信号质量
			case NVILIDAR_CMD_SET_NO_INTENSITIES:
			{
				if (data.length != sizeof(recv_info.isHasSensitive))
				{
					break;
				}

				memcpy((char *)(&recv_info.isHasSensitive), data.dataInfo, data.length);
				recv_info.recvFinishFlag = true;		//接收成功 

				//设置event失效 
				setNormalResponseUnlock();				//解锁 

				break;
			}
			case NVILIDAR_CMD_SET_AIMSPEED:		//目标转速
			{
				if (data.length != sizeof(recv_info.aimSpeed))
				{
					break;
				}

				memcpy((char *)(&recv_info.aimSpeed), data.dataInfo, data.length);
				recv_info.recvFinishFlag = true;		//接收成功 

				//设置event失效 
				setNormalResponseUnlock();				//解锁 

				break;
			}
			case NVILIDAR_CMD_SET_SAMPLING_RATE:	//采样率 
			{
				if (data.length != sizeof(recv_info.samplingRate))
				{
					break;
				}

				memcpy((char *)(&recv_info.samplingRate), data.dataInfo, data.length);
				recv_info.recvFinishFlag = true;		//接收成功 

				//设置event失效 
				setNormalResponseUnlock();				//解锁 

				break;
			}
			case NVILIDAR_CMD_SET_TAILING_LEVEL:	//写拖尾 
			{
				//printf("tail recv:\r\n");
				if (data.length != sizeof(recv_info.tailingLevel))
				{
					break;
				}

				memcpy((char *)(&recv_info.tailingLevel), data.dataInfo, data.length);
				recv_info.recvFinishFlag = true;		//接收成功 

				//设置event失效 
				setNormalResponseUnlock();				//解锁 

				break;
			}
			case NVILIDAR_CMD_GET_ANGLE_OFFSET:	//读角度偏移 
			case NVILIDAR_CMD_SET_ANGLE_OFFSET:	//读角度偏移 
			{
				if (data.length != sizeof(recv_info.angleOffset))
				{
					break;
				}

				memcpy((char *)(&recv_info.angleOffset), data.dataInfo, data.length);
				recv_info.recvFinishFlag = true;		//接收成功 

				//设置event失效 
				setNormalResponseUnlock();				//解锁 

				break;
			}
			default:
			{
				break;
			}
		}
	}

	//点云数据解包 
	bool LidarDriverSerialport::PointDataUnpack(uint8_t *buf,uint16_t len)
	{
		static Nvilidar_PointViewerPackageInfoTypeDef  pack_info;        //包信息

		static uint16_t    package_first_angle_temp = 0;   		//起始角
		static uint16_t    package_last_angle_temp = 0;    		//结束角
		static uint16_t    package_speed_temp = 0;              //转速信息
		static uint16_t    package_temperature_temp = 0;        //温度信息
		static uint32_t    package_after_0c_index = 0;          //0度后的第几包
		static uint16_t    checksum_speed_temp = 0;        		//校验计算
		static uint16_t    checksum_packnum_index = 0;     		//包数目和0位索引校验

		static int         recvPos = 0;							//当前接到的位置信息
		static size_t      remain_size = 0;						//接完包头剩下来的数据信息 

		//循环 
		for (int j = 0; j < len; j++)
		{
			uint8_t byte = buf[j];

			switch (recvPos)
			{
				case 0:     //第一个字节 包头
				{
					if (byte == (uint8_t)(NVILIDAR_POINT_HEADER & 0xFF))
					{
						recvPos++;      //index后移
						//printf("get first head\n");
					}
					else        //没收到 直接发下一包
					{
						break;
					}
					break;
				}
				case 1:     //第二字节  包头信息
				{
					if (byte == (uint8_t)(NVILIDAR_POINT_HEADER >> 8))
					{
						pack_info.packageCheckSumCalc = NVILIDAR_POINT_HEADER; //更新校验值
						recvPos++;      //index后移
						//printf("get second head\n");
					}
					else
					{
						pack_info.packageErrFlag = true;      //包头错误鸟
						recvPos = 0;
					}
					break;
				}
				case 2:     //频率或温度等信息
				{
					checksum_speed_temp = byte;     //校验赋值

					//0度角或其它信息
					if (1 == package_after_0c_index)  //其它  0位后第1包为  温度值
					{
						pack_info.packageHas0CFirst = false;
						pack_info.packageHasTempFirst = true;

						package_temperature_temp = byte;
					}
					else if (byte & 0x01)     //最低位是0位
					{
						pack_info.packageHas0CFirst = true;
						pack_info.packageHasTempFirst = false;

						package_speed_temp = byte;
					}
					else        //其它情况  该位置不含其它信息
					{
						pack_info.packageHas0CFirst = false;
						pack_info.packageHasTempFirst = false;
					}
					recvPos++;      //index后移
					break;
				}
				case 3:         //频率或者温度
				{
					checksum_speed_temp += (byte * 256);     //校验计算
					pack_info.packageCheckSumCalc ^= checksum_speed_temp; //校验计算

					static uint32_t pack_sum = 0;
					static uint32_t temp_sum = 0;

					if (pack_info.packageHas0CFirst)  //可能有0度
					{
						pack_info.packageHas0CFirst = false;
						pack_info.packageHasTemp = false;

						if (byte & 0x80)
						{
							//printf("pack_sum:%d\n", pack_sum);
							pack_sum = 0;

							package_after_0c_index = 0;     //0位包  则将0度后的个数  清0

							pack_info.packageHas0CAngle = true;
							package_speed_temp += ((uint16_t)byte * 256);
							pack_info.packageFreq = (package_speed_temp & 0x7FFF) >> 1;
						}
						else
						{
							pack_info.packageHas0CAngle = false;
						}
					}
					else if (pack_info.packageHasTempFirst)    //是温度计算信息
					{
						pack_sum++;

						pack_info.packageHasTempFirst = false;

						pack_info.packageHas0CAngle = false;


						pack_info.packageHasTemp = true;
						package_temperature_temp += ((uint16_t)byte * 256);
						pack_info.packageTemp = (int16_t)(package_temperature_temp);
					}
					else
					{
						pack_sum++;

						pack_info.packageHas0CAngle = false;
						pack_info.packageHasTemp = false;
					}
					package_after_0c_index++;     //0度后的包数目

					recvPos++;      //index后移
					break;
				}
				case 4:     //包数目
				{
					checksum_packnum_index = byte;

					if (byte != 0)
					{
						pack_info.packagePointNum = byte;
						recvPos++;      //index后移
					}
					else
					{
						pack_info.packagePointNum = 0;
						pack_info.packageErrFlag = true;      //包头错误鸟
						recvPos = 0;
					}
					break;
				}
				case 5:     //0度索引
				{
					checksum_packnum_index += (uint16_t)byte * 256;
					pack_info.packageCheckSumCalc ^= checksum_packnum_index; //校验计算

					if (pack_info.packageHas0CAngle)       //如果是0c  则会告知0c index
					{
						if (byte > 0)
						{
							pack_info.package0CIndex = byte - 1;      //0度角
						}
						else
						{
							pack_info.package0CIndex = 0;
						}
						//printf("0c index:%d\r\n",packageInfo.package0CIndex);
					}


					recvPos++;
					break;
				}
				case 6:             //起始角度低位
				{
					if (byte & NVILIDAR_RESP_MEASUREMENT_CHECKBIT)
					{
						package_first_angle_temp = byte;
						recvPos++;      //index后移
					}
					else
					{
						pack_info.packageErrFlag = true;
						recvPos = 0;
					}
					break;
				}
				case 7:             //起始角度高位
				{
					package_first_angle_temp += (uint16_t)byte * 256;
					pack_info.packageCheckSumCalc ^= package_first_angle_temp;
					pack_info.packageFirstAngle = package_first_angle_temp >> 1;

					//printf("first angle = %f\n",(float)pointViewerPackageInfo.packageFirstAngle/64.0f);

					recvPos++;      //index后移
					break;
				}
				case 8:             //结束角低位
				{
					if (byte & NVILIDAR_RESP_MEASUREMENT_CHECKBIT)
					{
						package_last_angle_temp = byte;

						//  printf("last_angle_l = %d\n",package_last_angle_temp);

						recvPos++;      //index后移
					}
					else
					{
						pack_info.packageErrFlag = true;
						recvPos = 0;
					}
					break;
				}
				case 9:             //结束角高位
				{
					package_last_angle_temp += (uint16_t)byte * 0x100;
					pack_info.packageCheckSumCalc ^= package_last_angle_temp;
					pack_info.packageLastAngle = package_last_angle_temp >> 1;

					//printf("last angle = %f\n",(float)packageInfo.packageLastAngle/64.0f);

					//计算每个角度之间的差值信息
					if (1 == pack_info.packagePointNum)  //只有一个点  则没有差值
					{
						pack_info.packageAngleDiffer = 0;
					}
					else
					{
						//结束角小于起始角
						if (pack_info.packageLastAngle < pack_info.packageFirstAngle)
						{
							//270~90度
							if ((pack_info.packageFirstAngle > 270 * NVILIDAR_ANGULDAR_RESOLUTION) && (pack_info.packageLastAngle < 90 * NVILIDAR_ANGULDAR_RESOLUTION))
							{
								pack_info.packageAngleDiffer =
									(float)((float)(360 * NVILIDAR_ANGULDAR_RESOLUTION + pack_info.packageLastAngle - pack_info.packageFirstAngle) /
									((float)(pack_info.packagePointNum - 1)));
								pack_info.packageLastAngleDiffer = pack_info.packageAngleDiffer;
							}
							else
							{
								pack_info.packageAngleDiffer = pack_info.packageLastAngleDiffer;
							}
						}
						//结束角大于等于起始角
						else
						{
							pack_info.packageAngleDiffer =
								(float)((float)(pack_info.packageLastAngle - pack_info.packageFirstAngle) /
								(float)(pack_info.packagePointNum - 1));
							pack_info.packageLastAngleDiffer = pack_info.packageAngleDiffer;
						}
					}

					recvPos++;      //index后移

					break;
				}
				case 10:    //校验低位
				{
					pack_info.packageCheckSumGet = byte;

					recvPos++;      //index后移
					break;
				}
				case 11:     //校验高位
				{
					pack_info.packageCheckSumGet += byte * 256;

					//计算基本信息 
					if (lidar_cfg.storePara.isHasSensitive)
					{
						pack_info.packagePointDistSize = 4;
					}
					else
					{
						pack_info.packagePointDistSize = 2;
					}
					remain_size = pack_info.packagePointNum * pack_info.packagePointDistSize; //剩余的距离数据信息


					recvPos++;      //index后移

					break;
				}
				default:
				{
					pack_info.packageBuffer.buf[recvPos] = byte;
					recvPos++;

					//所有数据接完了 
					if (recvPos == NVILIDAR_POINT_PACKAGE_HEAD_SIZE + remain_size)
					{
						uint16_t checksum_temp = 0;
						//计算校验
						for (size_t j = 0; j < remain_size; j++)
						{
							if (j % 2 == 0)
							{
								checksum_temp = pack_info.packageBuffer.buf[NVILIDAR_POINT_PACKAGE_HEAD_SIZE + j];  //低位
							}
							else
							{
								checksum_temp += (uint16_t)(pack_info.packageBuffer.buf[NVILIDAR_POINT_PACKAGE_HEAD_SIZE + j]) * 256;
								pack_info.packageCheckSumCalc ^= checksum_temp;
							}
						}
						//判断校验  
						if (pack_info.packageCheckSumCalc == pack_info.packageCheckSumGet)
						{
							//获取时间戳 起始&结束 (包尾时间 非真实时间) 
							if (pack_info.packageHas0CAngle)
							{
								pack_info.packageStamp = getStamp();
							}
							//计算一圈点的数据信息 
							PointDataAnalysis(pack_info);
						}
						//清空所有数据  
						memset((uint8_t *)(&pack_info), 0x00, sizeof(Nvilidar_PointViewerPackageInfoTypeDef));
						package_first_angle_temp = 0;   		//起始角
						package_last_angle_temp = 0;    		//结束角
						package_speed_temp = 0;              //转速信息
						package_temperature_temp = 0;        //温度信息
						checksum_speed_temp = 0;        		//校验计算
						checksum_packnum_index = 0;     		//包数目和0位索引校验
						recvPos = 0;                    //当前接到的位置信息
						remain_size = 0;			//接完包头剩下来的数据信息 
					}
					break;
				}
			}
		}
		return false;
	}

	//点云数据解包 
	void LidarDriverSerialport::PointDataAnalysis(Nvilidar_PointViewerPackageInfoTypeDef pack_point)
	{
		//点集信息 
		static std::vector<Nvilidar_Node_Info> point_list;
		static int curr_circle_count = 0;
		static int curr_pack_count = 0;

		//计算数据信息 
		for (int i = 0; i < pack_point.packagePointNum; i++)
		{
			Nvilidar_Node_Info node;

			if (lidar_cfg.storePara.isHasSensitive)
			{
				//点信息更新
				node.lidar_distance = pack_point.packageBuffer.pack_qua.package_Sample[i].PakageSampleDistance;     //距离
				node.lidar_quality = pack_point.packageBuffer.pack_qua.package_Sample[i].PakageSampleQuality;       //信号质量
				node.lidar_speed = (float)(pack_point.packageFreq) / 100.0;  //转速
				node.lidar_temper = (float)(pack_point.packageTemp) / 10.0;      //温度
				node.lidar_point_time = pack_point.packagePointTime;           //采样率
				node.lidar_index = i;                 //当前索引
				//是0度角
				if (pack_point.packageHas0CAngle)
				{
					if (i == pack_point.package0CIndex)
					{
						//printf("circle_count:%d\r\n", curr_pack_count);
						node.lidar_angle_zero_flag = true;
						curr_pack_count = 0;
					}
					else
					{
						curr_pack_count++;
						node.lidar_angle_zero_flag = false;
					}
				}
				else
				{
					curr_pack_count++;
					node.lidar_angle_zero_flag = false;
				}

				//角度计算
				node.lidar_angle = (float)(pack_point.packageFirstAngle + i * pack_point.packageAngleDiffer)
					/ (float)(NVILIDAR_ANGULDAR_RESOLUTION);
				if(node.lidar_angle >= 360.0)
				{
					node.lidar_angle -= 360.0;
				}
			}
			else
			{
				//点信息更新
				node.lidar_distance = pack_point.packageBuffer.pack_no_qua.package_Sample[i].PakageSampleDistance;     //距离
				node.lidar_quality = 0;
				node.lidar_speed = (float)(pack_point.packageFreq) / 100.0;  	//转速
				node.lidar_temper = (float)(pack_point.packageTemp) / 10.0;     //温度
				node.lidar_point_time = pack_point.packagePointTime;           	//采样率
				node.lidar_index = i;                 //当前索引
				//是0度角
				if (pack_point.packageHas0CAngle)
				{
					if (i == pack_point.package0CIndex)
					{
						node.lidar_angle_zero_flag = true;
						curr_pack_count = 0;
					}
					else
					{
						node.lidar_angle_zero_flag = false;
						curr_pack_count++;
					}
				}
				else
				{
					node.lidar_angle_zero_flag = false;
					curr_pack_count++; 
				}

				//角度计算
				node.lidar_angle = (float)(pack_point.packageFirstAngle + i * pack_point.packageAngleDiffer)
					/ (float)(NVILIDAR_ANGULDAR_RESOLUTION);
				if (node.lidar_angle >= 360.0)
				{
					node.lidar_angle -= 360.0;
				}
			}

			//追加到数据区内 
			if(node.lidar_angle_zero_flag)
			{
				curr_circle_count = point_list.size();		//取到一圈的真实的点数信息 
			}
			point_list.push_back(node);
		}

		//找到点数信息 
		if(pack_point.packageHas0CAngle)
		{
			uint32_t  all_pack_time;
			uint32_t  all_count = 0;
			uint32_t  circle_count = 0;
			uint64_t  stamp_temp = 0;
			uint64_t  stamp_differ = 0;

			m_run_circles++;		//包数目++ 

			circleDataInfo.lidarCircleNodePoints = point_list;	//上个零位包开始到本包结束了（到结尾，用来算真实时间戳）
			all_count = point_list.size();
			circle_count = curr_circle_count;

			circleDataInfo.lidarCircleNodePoints.assign(point_list.begin(),point_list.begin() + curr_circle_count);		//取前半部分的值 	
			point_list.erase(point_list.begin(),point_list.begin() + curr_circle_count);			//后半部分  下一圈的数据 erase掉 
			curr_circle_count = 0;

			//计算时间差  按比例减掉多传的时间  最后一包 踢掉后面的点所用的时间  重算时间戳 
			if (circleDataInfo.stopStamp == 0)
			{
				stamp_temp = pack_point.packageStamp;
			}
			else
			{
				stamp_differ = pack_point.packageStamp - circleDataInfo.stopStamp;
				stamp_temp = pack_point.packageStamp - (stamp_differ * (all_count - circle_count) / all_count);
			}

			//计算 
			circleDataInfo.startStamp = circleDataInfo.stopStamp;
			//判断是否有非法值 
			if (m_run_circles <= 8)
			{
				circleDataInfo.stopStamp = pack_point.packageStamp;
			}
			else
			{
				circleDataInfo.stopStamp = stamp_temp;

				//uint64_t diff = circleDataInfo.stopStamp - circleDataInfo.startStamp;
				//printf("time differ:%lu\r\n", diff);
			}
			//按比例重算结束时间 因为一包固定128点 0位可能出在任意位置  会影响时间戳精确度 
			//printf("pack_num:%d,indx:%d\r\n", circleDataInfo.lidarCircleNodePoints.size(), pack_point.package0CIndex);

			if (m_run_circles > 3)
			{
				setCircleResponseUnlock();		//解锁  告知已接到一包数据信息 
			}

			delayMS(5);
		}
	}


	//-------------------------------------对外接口信息-------------------------------------------

	//获取SDK版本号
	std::string LidarDriverSerialport::getSDKVersion()
	{
		return NVILIDAR_SDKVerision;
	}

	//获取串口列表信息
	std::vector<NvilidarSerialPortInfo> LidarDriverSerialport::getPortList()
	{
		std::vector<NvilidarSerialPortInfo> nvi_serial_list;
		NvilidarSerialPortInfo nvi_serial;

		nvi_serial_list.clear();

	   std::vector<NvilidarPortInfo> lst = nvilidar_list_ports();

		#if defined (_WIN32)
			for (std::vector<NvilidarPortInfo>::iterator it = lst.begin(); it != lst.end(); it++)
			{
				nvi_serial.portName = (*it).port;
				nvi_serial.description = (*it).description;

				nvi_serial_list.push_back(nvi_serial);
			}
		#else
			for (std::vector<NvilidarPortInfo>::iterator it = lst.begin(); it != lst.end(); it++)
			{
				//printf("port:%s,des:%s\n",(*it).port.c_str(),(*it).description.c_str());
				//usb check
				if ((*it).port.find("ttyACM") != std::string::npos)      //linux ttyacm
				{
					nvi_serial.portName = (*it).port;
					nvi_serial.description = (*it).description;

					nvi_serial_list.push_back(nvi_serial);
				}
			}
		#endif

		return nvi_serial_list;
	}

	//设置雷达是否带信号质量信息
	bool LidarDriverSerialport::SetIntensities(const uint8_t has_intensity, uint32_t timeout)
	{
		recv_info.recvFinishFlag = false;

		uint8_t   cmd;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//刷新串口 并做接收处理
		cmd = (has_intensity ? NVILIDAR_CMD_SET_HAVE_INTENSITIES : NVILIDAR_CMD_SET_NO_INTENSITIES);
		//发送命令
		if (!SendCommand(cmd))
		{
			return false;
		}

		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				return true;
			}
		}

		return false;
	}

	//获取设备类型信息
	bool LidarDriverSerialport::GetDeviceInfo(Nvilidar_DeviceInfo &info, uint32_t timeout)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_GET_DEVICE_INFO))
		{
			return false;
		}
	
		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if(recv_info.recvFinishFlag)
			{
				uint8_t productNameTemp[6] = { 0 };
				memcpy(productNameTemp, recv_info.lidar_device_info.MODEL_NUM,5);

				//生成字符信息
				info.m_SoftVer = formatString("V%d.%d", recv_info.lidar_device_info.SW_V[0], recv_info.lidar_device_info.SW_V[1]);
				info.m_HardVer = formatString("V%d.%d", recv_info.lidar_device_info.HW_V[0], recv_info.lidar_device_info.HW_V[1]);
				info.m_ProductName = formatString("%s", productNameTemp);
				info.m_SerialNum = formatString("%01d%01d%01d%01d%01d%01d%01d%01d%01d%01d%01d%01d%01d%01d%01d%01d",
					recv_info.lidar_device_info.serialnum[0], recv_info.lidar_device_info.serialnum[1], recv_info.lidar_device_info.serialnum[2], recv_info.lidar_device_info.serialnum[3],
					recv_info.lidar_device_info.serialnum[4], recv_info.lidar_device_info.serialnum[5], recv_info.lidar_device_info.serialnum[6], recv_info.lidar_device_info.serialnum[7],
					recv_info.lidar_device_info.serialnum[8], recv_info.lidar_device_info.serialnum[9], recv_info.lidar_device_info.serialnum[10], recv_info.lidar_device_info.serialnum[11],
					recv_info.lidar_device_info.serialnum[12], recv_info.lidar_device_info.serialnum[13], recv_info.lidar_device_info.serialnum[14], recv_info.lidar_device_info.serialnum[15]);


				return true;
			}
		}

		return false;
	}


	//复位雷达
	bool LidarDriverSerialport::Reset(void)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		if (!SendCommand(NVILIDAR_CMD_GET_DEVICE_INFO))
		{
			return false;
		}

		delayMS(800);

		return true;
	}

	//设置雷达转速信息
	bool LidarDriverSerialport::SetScanMotorSpeed(uint16_t frequency, uint16_t &ret_frequency,
		uint32_t timeout)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_SET_AIMSPEED,
			((uint8_t*)(&frequency)), sizeof(frequency)))
		{
			return false;
		}

		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				ret_frequency = recv_info.aimSpeed;

				return true;
			}
		}

		return false;
	}

	//增加雷达采样率
	bool LidarDriverSerialport::SetSamplingRate(uint32_t rate_write, uint32_t &rate,
		uint32_t timeout)   //雷达采样率增加
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_SET_SAMPLING_RATE,
			((uint8_t*)(&rate_write)), sizeof(rate_write)))
		{
			return false;
		}
 
		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				rate = recv_info.samplingRate;

				return true;
			}
		}

		return false;
	}


	//读取角度偏移
	bool LidarDriverSerialport::GetZeroOffsetAngle(int16_t &angle, uint32_t timeout)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_GET_ANGLE_OFFSET))
		{
			return false;
		}

		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				angle = recv_info.angleOffset;

				return true;
			}
		}

		return false;
	}

	//设置角度偏移
	bool LidarDriverSerialport::SetZeroOffsetAngle(int16_t angle_set, int16_t &angle,
		uint32_t timeout)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_SET_ANGLE_OFFSET,
			((uint8_t*)(&angle_set)), sizeof(angle_set)))
		{
			return false;
		}

		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				angle = recv_info.angleOffset;

				return true;
			}
		}

		return false;
	}

	//设置拖尾等级
	bool LidarDriverSerialport::SetTrailingLevel(uint8_t tailing_set, uint8_t &tailing,
		uint32_t  timeout)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_SET_TAILING_LEVEL, (uint8_t *)(&tailing_set), sizeof(tailing)))
		{
			return false;
		}

		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				tailing = recv_info.tailingLevel;

				return true;
			}
		}

		return false;
	}

	//获取雷达配置信息
	bool LidarDriverSerialport::GetLidarCfg(Nvilidar_StoreConfigTypeDef &info, uint32_t timeout)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_GET_LIDAR_CFG))
		{
			return false;
		}

		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				//生成字符信息
				info.aimSpeed = recv_info.lidar_get_para.aimSpeed;
				info.isHasSensitive = recv_info.lidar_get_para.hasSensitive;
				info.samplingRate = recv_info.lidar_get_para.samplingRate;
				info.tailingLevel = recv_info.lidar_get_para.tailingLevel;

				return true;
			}
		}

		return false;
	}

	//保存雷达参数
	bool LidarDriverSerialport::SaveCfg(bool &flag, uint32_t timeout)
	{
		recv_info.recvFinishFlag = false;

		//先停止雷达 如果雷达在运行 
		if(lidar_state.m_Scanning)
		{
			StopScan();
		}

		//发送命令
		if (!SendCommand(NVILIDAR_CMD_SAVE_LIDAR_PARA))
		{
			return false;
		}

		//等待线程同步 超时 
		if (waitNormalResponse(timeout))
		{
			if (recv_info.recvFinishFlag)
			{
				if (recv_info.saveFlag)
				{
					return true;
				}
			}
		}

		return false;
	}

	//获取当前扫描状态
	bool LidarDriverSerialport::LidarGetScanState()
	{
		return lidar_state.m_Scanning;
	}

	//---------------------------------------------多线程API----------------------------------------------
	//初始化线程 
	bool LidarDriverSerialport::createThread()
	{
		#if	defined(_WIN32)
			/*
			创建线程函数详细说明
			*HANDLE WINAPI CreateThread(
			__in_opt  LPSECURITY_ATTRIBUTES lpThreadAttributes,
			__in      SIZE_T dwStackSize,
			__in      LPTHREAD_START_ROUTINE lpStartAddress,
			__in_opt __deref __drv_aliasesMem LPVOID lpParameter,
			__in      DWORD dwCreationFlags,
			__out_opt LPDWORD lpThreadId
			);
			*返回值：函数成功，返回线程句柄；函数失败返回false。若不想返回线程ID,设置值为NULL
			*参数说明：
			*lpThreadAttributes	线程安全性，使用缺省安全性,一般缺省null
			*dwStackSize	堆栈大小，0为缺省大小
			*lpStartAddress	线程要执行的函数指针，即入口函数
			*lpParameter	线程参数
			*dwCreationFlags	线程标记，如为0，则创建后立即运行
			*lpThreadId	LPDWORD为返回值类型，一般传递地址去接收线程的标识符，一般设为null
			*/
			_thread = CreateThread(NULL, 0, LidarDriverSerialport::periodThread, this, 0, NULL);
			if (_thread == NULL)
			{
				return false;
			}



			/*
			*创建事件对象
			HANDLE WINAPI CreateEventA(
			__in_opt LPSECURITY_ATTRIBUTES lpEventAttributes,
			__in     BOOL bManualReset,
			__in     BOOL bInitialState,
			__in_opt LPCSTR lpName
			);
			*返回值：如果函数调用成功，函数返回事件对象的句柄。如果对于命名的对象，在函数调用前已经被创建，函数将返回存在的事件对象的句柄，而且在GetLastError函数中返回ERROR_ALREADY_EXISTS。
			如果函数失败，函数返回值为NULL，如果需要获得详细的错误信息，需要调用GetLastError。
			*参数说明：
			*lpEventAttributes	安全性，采用null默认安全性。
			*bManualReset	（TRUE）人工重置或（FALSE）自动重置事件对象为非信号状态，若设为人工重置，则当事件为有信号状态时，所有等待的线程都变为可调度线程。
			*bInitialState		指定事件对象的初始化状态，TRUE：初始为有信号状态。
			*lpName	事件对象的名字，一般null匿名即可
			*/
			_event_analysis = CreateEvent(NULL, false, false, NULL);;
			if (_event_analysis == NULL)
			{
				return false;
			}
			ResetEvent(_event_analysis);

			//一圈点的数据信息 信息同步 
			_event_circle = CreateEvent(NULL, false, false, NULL);;
			if (_event_circle == NULL)
			{
				return false;
			}
			ResetEvent(_event_circle);

			return true;
		#else 
			//正常协议解析同步  
			pthread_cond_init(&_cond_analysis, NULL);
    		pthread_mutex_init(&_mutex_analysis, NULL);
			pthread_cond_init(&_cond_point, NULL);
    		pthread_mutex_init(&_mutex_point, NULL);

			/* 创建线程pthread */
     		if(-1 == pthread_create(&_thread, NULL, LidarDriverSerialport::periodThread, this))
     		{
				 _thread = -1;
         		return false;
			}

			return true;

		#endif 
	}

	//关闭线程 
	void LidarDriverSerialport::closeThread()
	{
		#if	defined(_WIN32)
			CloseHandle(_thread);
			CloseHandle(_event_analysis);
			CloseHandle(_event_circle);
		#else 
			pthread_cancel(_thread);
			pthread_cond_destroy(&_cond_analysis);
			pthread_mutex_destroy(&_mutex_analysis);
			pthread_cond_destroy(&_cond_point);
			pthread_mutex_destroy(&_mutex_point);

		#endif 
	}

	//等待事件 
	bool LidarDriverSerialport::waitNormalResponse(uint32_t timeout)
	{
		#if	defined(_WIN32)
			DWORD state;
			ResetEvent(_event_analysis);		// 重置事件，让其他线程继续等待（相当于获取锁）
			state = WaitForSingleObject(_event_analysis, timeout);
			if(state == WAIT_OBJECT_0)
			{
				return true;
			}
		#else 
			struct timeval now;
    		struct timespec outtime;
			int state = -1;

			pthread_mutex_lock(&_mutex_analysis);
 
			gettimeofday(&now, NULL);
			outtime.tv_sec = now.tv_sec + timeout / 1000;
			outtime.tv_nsec = now.tv_usec * 1000 + timeout%1000*1000;
		
			state = pthread_cond_timedwait(&_cond_analysis, &_mutex_analysis, &outtime);
			pthread_mutex_unlock(&_mutex_analysis);

			if(0 == state)
			{
				return true;
			}

		#endif

		return false;
	}

	//等待事件 解锁 
	void LidarDriverSerialport::setNormalResponseUnlock()
	{
		#if	defined(_WIN32)
			SetEvent(_event_analysis);			// 重置事件，让其他线程继续等待（相当于获取锁）
		#else 
			pthread_mutex_lock(&_mutex_analysis);
    		pthread_cond_signal(&_cond_analysis);
    		pthread_mutex_unlock(&_mutex_analysis);
		#endif 
	}

	//等待一圈点云 事件 
	bool LidarDriverSerialport::waitCircleResponse(CircleDataInfoTypeDef &info, uint32_t timeout)
	{
		//等待解锁  即一圈点数据完成了  
		#if	defined(_WIN32)
			DWORD state;
			ResetEvent(_event_circle);		// 重置事件，让其他线程继续等待（相当于获取锁）
			state = WaitForSingleObject(_event_circle, timeout);
			if (state == WAIT_OBJECT_0)
			{
				//数据追加入 
				info = circleDataInfo;

				return true;
			}	
		#else 
			struct timeval now;
    		struct timespec outtime;
			int state = -1;

			pthread_mutex_lock(&_mutex_point);
 
			gettimeofday(&now, NULL);
			outtime.tv_sec = now.tv_sec + timeout / 1000;
			outtime.tv_nsec = now.tv_usec * 1000 + timeout%1000*1000;
		
			state = pthread_cond_timedwait(&_cond_point, &_mutex_point, &outtime);
			pthread_mutex_unlock(&_mutex_point);

			if(0 == state)
			{
				//数据更新 
				info = circleDataInfo;

				return true;
			}
		#endif

		return false;
	}

	//等待一圈点云 事件 解锁 
	void LidarDriverSerialport::setCircleResponseUnlock()
	{
		#if	defined(_WIN32)
			SetEvent(_event_circle);			// 重置事件，让其他线程继续等待（相当于获取锁）
		#else 
			pthread_mutex_lock(&_mutex_point);
    		pthread_cond_signal(&_cond_point);
    		pthread_mutex_unlock(&_mutex_point);
		#endif 
	}

	//线程进程 分win32和linux等   
	#if	defined(_WIN32)
		DWORD WINAPI  LidarDriverSerialport::periodThread(LPVOID lpParameter)
		{
			static uint8_t recv_data[8192];
			size_t recv_len = 0;

			//在线程中要做的事情
			LidarDriverSerialport *pObj = (LidarDriverSerialport *)lpParameter;   //传入的参数转化为类对象指针

			while (pObj->lidar_state.m_SerialOpen)
			{	
				//解包处理 === 正常解包 
				if (! pObj->lidar_state.m_Scanning)	//点云数据包 
				{
					//读串口接收数据长度 
					recv_len = pObj->serialport.serialReadData(recv_data, 8192);
					if(recv_len > 0)
					{
						pObj->NormalDataUnpack(recv_data, recv_len);
					}
					delayMS(3);		//必须要加sleep 不然会超高占用cpu	
				}
				//解包处理 ==== 点云解包 
				else 
				{
					//读串口接收数据长度 
					recv_len = pObj->serialport.serialReadData(recv_data, 8192);
					if (recv_len > 0)
					{
						pObj->PointDataUnpack(recv_data, recv_len);
					}
				}
			}

			return 0;
		}
	#else 
		/* 定义线程pthread */
	   	void * LidarDriverSerialport::periodThread(void *lpParameter)       
		{
			static uint8_t recv_data[8192];
			size_t recv_len = 0;

			//在线程中要做的事情
			LidarDriverSerialport *pObj = (LidarDriverSerialport *)lpParameter;   //传入的参数转化为类对象指针

			while (pObj->lidar_state.m_SerialOpen)
			{	
				//解包处理 === 正常解包 
				if (! pObj->lidar_state.m_Scanning)	//点云数据包 
				{
					//读串口接收数据长度 
					recv_len = pObj->serialport.serialReadData(recv_data, 8192);
					if(recv_len > 0)
					{
						pObj->NormalDataUnpack(recv_data, recv_len);
					}
					delayMS(3);		//必须要加sleep 不然会超高占用cpu	
				}
				//解包处理 ==== 点云解包 
				else 
				{
					//读串口接收数据长度 
					recv_len = pObj->serialport.serialReadData(recv_data, 8192);
					if(recv_len > 0)
					{
						pObj->PointDataUnpack(recv_data, recv_len);
					}
				}
			}

			return 0;
		}
	#endif 

	
}




