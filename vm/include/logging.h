/*
 *  Virtual Machine using Breakpoint Tracing
 *  Copyright (C) 2012 Bi Wu
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef V_LOGGING_H
#define V_LOGGING_H
#include <linux/module.h>
#include <linux/kernel.h>

#define LOGLEVEL 0

#ifdef LOGLEVEL

/*#if LOGLEVEL < 5
#define V_VERBOSE(fmt, args...)
#else
#define V_VERBOSE(fmt, args...) printk(KERN_ERR "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 4
#define V_LOG(fmt, args...)
#else
#define V_LOG(fmt, args...) printk(KERN_ERR "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 3
#define V_EVENT(fmt, args...)
#else
#define V_EVENT(fmt, args...) printk(KERN_ERR "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 2
#define V_ALERT(fmt, args...)
#else
#define V_ALERT(fmt, args...) printk(KERN_ERR "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 1
#define V_ERR(fmt, args...)
#else
#define V_ERR(fmt, args...) printk(KERN_ERR "BTC: " fmt, ##args)
#endif
*/

#if LOGLEVEL < 5
#define V_VERBOSE(fmt, args...)
#else
#define V_VERBOSE(fmt, args...) printk(KERN_DEBUG "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 4
#define V_LOG(fmt, args...)
#else
#define V_LOG(fmt, args...) printk(KERN_INFO "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 3
#define V_EVENT(fmt, args...)
#else
#define V_EVENT(fmt, args...) printk(KERN_NOTICE "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 2
#define V_ALERT(fmt, args...)
#else
#define V_ALERT(fmt, args...) printk(KERN_WARNING "BTC: " fmt, ##args)
#endif

#if LOGLEVEL < 1
#define V_ERR(fmt, args...)
#else
#define V_ERR(fmt, args...) printk(KERN_ERR "BTC: " fmt, ##args)
#endif

#else

/*default: only output error messages */

#define V_VERBOSE(fmt, args...)
#define V_LOG(fmt, args...)
#define V_EVENT(fmt, args...)
#define V_ALERT(fmt, args...)
#define V_ERR(fmt, args...) printk(KERN_ERR "BTC: " fmt, ##args)

#endif

#endif
