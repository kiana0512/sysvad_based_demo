#pragma once
#include <ks.h>
#include <ksmedia.h>

// ===== 工具宏 =====
#ifndef RTL_NUMBER_OF
#define RTL_NUMBER_OF(A) (sizeof(A)/sizeof((A)[0]))
#endif

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

// ===== WDK 版本兼容：Pin 描述取 DataRanges =====
// 如果你编译时报 “DataRanges 不是 KSPIN_DESCRIPTOR_EX 的成员”，就在包含 KsHelper.h 之前
// #define USE_PIN_DESCRIPTOR_EX_COMPAT 以走下面这个分支。
#ifndef USE_PIN_DESCRIPTOR_EX_COMPAT
  #define PIN_DATA_RANGES_COUNT(pinEx)   ((pinEx).PinDescriptor.DataRangesCount)
  #define PIN_DATA_RANGES(pinEx)         ((pinEx).PinDescriptor.DataRanges)
#else
  #define PIN_DATA_RANGES_COUNT(pinEx)   ((pinEx).DataRangesCount)
  #define PIN_DATA_RANGES(pinEx)         ((pinEx).DataRanges)
#endif

// ===== WDK 版本兼容：拓扑连接字段名 =====
// 如果你编译时报 “FromNode/ToPin 不是 KSTOPOLOGY_CONNECTION 的成员”，
// 就在包含 KsHelper.h 之前 #define USE_TOPO_CONNECTION_COMPAT。
#ifndef USE_TOPO_CONNECTION_COMPAT
  #define CONN_FROM_NODE(conn)   ((conn).FromNode)
  #define CONN_FROM_PIN(conn)    ((conn).FromPin)
  #define CONN_TO_NODE(conn)     ((conn).ToNode)
  #define CONN_TO_PIN(conn)      ((conn).ToPin)
#else
  #define CONN_FROM_NODE(conn)   ((conn).FromNodePin.Node)
  #define CONN_FROM_PIN(conn)    ((conn).FromNodePin.Pin)
  #define CONN_TO_NODE(conn)     ((conn).ToNodePin.Node)
  #define CONN_TO_PIN(conn)      ((conn).ToNodePin.Pin)
#endif
