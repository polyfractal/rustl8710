#!/bin/sh

OS=`uname | cut -c 1-6`

PID=`ps aux | grep openocd | gawk '{print $1}'`

if [ ! -z "$PID" -a "$PID" != " "  ]; then
	echo Found openocd running, Kill it
	kill $PID
else
	if [ $OS == CYGWIN ]; then
		echo Try to search windows process
		PID=`ps --windows | grep openocd | gawk '{print $1}'`
		if [ -n PID ]; then
			echo Found openocd running, Kill it
			taskkill /F /pid $PID
		fi
	fi
fi

openocd -f interface/cmsis-dap.cfg -f component/soc/realtek/8195a/misc/gcc_utility/openocd/ameba1.cfg
