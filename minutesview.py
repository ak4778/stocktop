#!/usr/bin/python
#encoding=utf-8
#-*- coding: utf-8 -*-

import os
import sys
import conf
import time
import talib
import locale
import random
import datetime
import indicators
import numpy as np
import pandas as pd
import tushare as ts
import realtime_quotes as rq
import matplotlib.pyplot as plt
from tushare.util import dateu as du
from PyQt4 import QtGui,QtCore,Qt
from PyQt4.QtGui import *
from PyQt4.Qt import *

ticks_today = {}
ticks_today['time']   = ['','','','','','','','','','']
ticks_today['price']  = ['','','','','','','','','','']
ticks_today['volume'] = ['','','','','','','','','','']
ticks_today['type']   = ['','','','','','','','','','']

#du.get_now()[11:16]
def get_trading_time_index():
    now = du.get_now()[11:16]
    hour = int(now[0:2])
    minute = int(now[3:5]) 
    print "latest trading day : ", du.today()
    print "now :", now, ", hour :", hour, ", minute :", minute
    print "get_now() day = ", du.get_now()[0:10]
    if (du.get_now()[0:10] != du.today()):
        print "eeeeeeee"
        return -1

    try:
        index = conf.TimeStamps.index(now)
        return index
    except Exception as e:
        print "e hour",int(hour), " min", int(minute)
        if (int(hour) >= 12 and int(hour) < 13) or (int(hour) == 11 and int(minute) > 30):
            print "11:30"
            return conf.TimeStamps.index('11:30')
        elif ((int(hour) > 15) or (int(hour) == 15 and int(minute) > 0)):
            print "15:00"
            return conf.TimeStamps.index('15:00')
        return -1

class Ticks_Reading_Thread(QtCore.QThread):

    rendered_today_ticks = QtCore.pyqtSignal(str)
    rendered_realtime = QtCore.pyqtSignal(str)
  
    updateSdata = QtCore.pyqtSignal(dict)

    def __init__(self, stockid, parent=None):
        super(Ticks_Reading_Thread, self).__init__(parent)
        self.sid = stockid
        self.abort = False
        self.thread_running = 1
        self.thread_finished = 0
    
    def __del__(self):
        print "tickthread delete"
        #self.mutex.lock()
        self.abort = True
        #self.condition.wakeOne()
        #self.mutex.unlock()
        self.wait()

    def run(self):
        while(self.thread_running == 1):
            if (self.thread_running == 0):
                self.thread_finished = 1
                return
            if self.abort:
                return

            if (self.thread_running == 0):
                self.thread_finished = 1
                return

            try:
                df = get_all_ticks_today(self.sid)
                if df is not None:
                    df = df.head(10)
                    dflen = df.shape[0] 
                    for i in range(0,dflen):
                        if (self.thread_running == 0):
                            self.thread_finished = 1
                            return
                        if self.abort:
                            return
                        ticks_today['time'][i]   = str(df['time'][i])
                        if self.abort:
                            return
                        if (self.thread_running == 0):
                            self.thread_finished = 1
                            return
                        ticks_today['price'][i]  = str(df['price'][i])
                        if self.abort:
                            return
                        if (self.thread_running == 0):
                            self.thread_finished = 1
                            return
                        ticks_today['volume'][i] = str(df['volume'][i])
                        if self.abort:
                            return
                        if (self.thread_running == 0):
                            self.thread_finished = 1
                            return
                        ticks_today['type'][i]   = str(df['type'][i])
                    print "\nget_today_tick() ok\n"
                    self.rendered_today_ticks.emit(str(time.strftime('%Y-%m-%d %H:%M:%S')))
                else:  
                    print "Nan to get_today_tick()"
            except Exception as e:
                print "failed to get_today_ticks from website"
                print(e)
            if (self.thread_running == 0):
                self.thread_finished = 1
                return
#            self.updateSdata.emit
            time.sleep(2)
        self.thread_finished = 1
 
class Pankou_Reading_Thread(QtCore.QThread):

    rendered_realtime = QtCore.pyqtSignal(str)
  
    updateSdata = QtCore.pyqtSignal(dict)

    def __init__(self, stockid, parent=None):
        super(Pankou_Reading_Thread, self).__init__(parent)
        self.sid = stockid
        self.abort = False
        self.thread_running = 1
        self.thread_finished = 0
    
    def __del__(self):
        print "pankouthread delete"
        #self.mutex.lock()
        self.abort = True
        #self.condition.wakeOne()
        #self.mutex.unlock()
        self.wait()

    def run(self):
        while(self.thread_running == 1):
            if (self.thread_running == 0):
                self.thread_finished = 1
                return
            if self.abort:
                return
            try:
                # get pankou data from website
                indicators.get_realtime(self.sid)
                self.rendered_realtime.emit(str(time.strftime('%Y-%m-%d %H:%M:%S')))
            except Exception as e:
                print "failed to get_realtime from website"
                print(e)

            if (self.thread_running == 0):
                self.thread_finished = 1
                return
            time.sleep(1)
        self.thread_finished = 1

class real_painter:
    '''绘制行情类'''
    def __init__(self,parent):
         
        #初始化
        self.parent = parent
        self.painter = QtGui.QPainter()
        self.painter.begin(self.parent)
 
        #设置抗锯齿
        #self.painter.setRenderHint(QtGui.QPainter.Antialiasing)
        #度量尺对象
        self.metrics = self.painter.fontMetrics()
         
        #设置字体库
        self.fonts = dict()
        self.fonts['default'] = QtGui.QFont('Serif', 9, QtGui.QFont.Normal)
        self.fonts['yahei_14_bold']= QtGui.QFont('Serif',12,QtGui.QFont.Bold)
        self.fonts['yahei_18_bold']= QtGui.QFont('Serif',20,QtGui.QFont.Bold)
        self.fonts['yahei_14']= QtGui.QFont('Serif',12,QtGui.QFont.Light)
        self.fonts['yahei_14_normal']= QtGui.QFont('Serif',12,QtGui.QFont.Normal)
        self.setFont('default')
 
        #设置笔刷样式库
        self.pens = dict()
         
        #红色 1px粗  1px点 2px距 线条
        self.pens['red_1px_dashline'] =  QtGui.QPen( QtCore.Qt.red, 1, QtCore.Qt.DashLine)
        self.pens['red_1px_dashline'].setDashPattern([1,2])
 
        #红色 1px粗 实线条
        self.pens['red'] = QtGui.QPen( QtCore.Qt.red, 1, QtCore.Qt.SolidLine)
        #红色 3px粗 实线条
        self.pens['red_2px'] = QtGui.QPen( QtCore.Qt.red, 2, QtCore.Qt.SolidLine)
        #红色 2px粗 实线条
        self.pens['red_3px'] = QtGui.QPen( QtCore.Qt.red, 3, QtCore.Qt.SolidLine)
        #黄色 1px粗 实线条
        self.pens['yellow'] = QtGui.QPen( QtCore.Qt.yellow, 1, QtCore.Qt.SolidLine)
        #白色 1px粗 实线条
        self.pens['white']  = QtGui.QPen( QtCore.Qt.white , 1, QtCore.Qt.SolidLine)
        #灰色 1px粗 实线条
        self.pens['gray']   = QtGui.QPen( QtCore.Qt.gray, 1, QtCore.Qt.SolidLine)
        self.pens['blue']  = QtGui.QPen( QColor(64,64,255) , 1, QtCore.Qt.SolidLine)
        self.pens['magan']  = QtGui.QPen( QColor(255,0,255) , 1, QtCore.Qt.SolidLine)

        #绿色 1px粗 实线条
        self.pens['green']   = QtGui.QPen( QtCore.Qt.green, 1, QtCore.Qt.SolidLine)
        #绿色 3px粗 实线条
        self.pens['green_2px']   = QtGui.QPen( QtCore.Qt.green, 2, QtCore.Qt.SolidLine)
        #亮蓝 1px粗  1px点 2px距 线条
        self.pens['cyan_1px_dashline'] =  QtGui.QPen( QtCore.Qt.cyan, 1, QtCore.Qt.DashLine)
        self.pens['cyan_1px_dashline'].setDashPattern([3,2])
        #获得窗口的长和宽
        size      = self.parent.size()
        self.w    = size.width()
        self.h    = size.height()
 
        #设置grid的上下左右补丁边距
        self.titley = 20
        self.totalRealtimeK = 242.0
        self.left_text_padding   = 6
        self.grid_padding_left   = 58  #左侧补丁边距
        self.grid_padding_right  = 218 #右侧补丁边距
        self.percent_width = 52  #底部补丁边距
        self.grid_padding_right += self.percent_width
        self.grid_padding_top    = 25  #顶部补丁边距
        self.grid_padding_bottom = 17  #底部补丁边距

        self.ratio               = 0.65 #price/volume
             
        #开始绘制
        self.start_paint()
 
        self.painter.end()   #结束

    '''绘制流程步骤'''
    def start_paint(self):
#        if (not (self.parent.m_x >= self.grid_padding_left and self.parent.m_x<=self.w-self.grid_padding_right and self.parent.m_y>=self.grid_padding_top and self.parent.m_y<=self.h-self.grid_padding_bottom)):
#            self.current_moving_item = -1
        self.PriceGridPaint()
#        self.rightGridPaint()
        self.hrightGridPaint()
        self.timelinePaint()
        self.topInfoPaint()
        self.rulerPaint()
        self.VolumeGridPaint()
        self.volumePaint()
        self.pricePaint()
        self.xyPaint()

    '''设置使用的字体'''
    def setFont(self,code='default'):
        self.painter.setFont(self.fonts[code])
         
    '''设置使用的笔刷'''
    def setPen(self,code='default'):
        self.painter.setPen(self.pens[code])
         
    '''绘制股价走势表格'''
    def PriceGridPaint(self):
        self.setPen('red')
        self.painter.setBrush(QtCore.Qt.NoBrush)
         
        sum_width  = self.grid_padding_left+self.grid_padding_right
        sum_height = self.grid_padding_top+self.grid_padding_bottom
 
        grid_height = self.h-sum_height
 
        #画边框
        self.painter.drawRect(self.grid_padding_left, self.grid_padding_top, self.w-sum_width, self.h-sum_height)

        #成交量和走势的分界线
        self.painter.drawLine(self.grid_padding_left, grid_height*self.ratio + self.grid_padding_top,
                            self.w-self.grid_padding_right, grid_height*self.ratio + self.grid_padding_top)
 
        #股票昨收中间线
        self.painter.drawLine(self.grid_padding_left+1,
                            grid_height*self.ratio/2.0+self.grid_padding_top,
                            self.w-self.grid_padding_right
                            ,grid_height*self.ratio/2.0+self.grid_padding_top)
 
        #其他线条
        self.painter.drawLine(0,self.h-self.grid_padding_bottom,self.w-self.grid_padding_right+self.percent_width,self.h-self.grid_padding_bottom)
        self.painter.drawLine(0,self.h-self.grid_padding_bottom+16,self.w,self.h-self.grid_padding_bottom+16)
 
        self.painter.drawLine(self.w-self.grid_padding_right,0,
                            self.w-self.grid_padding_right,self.h-self.grid_padding_bottom+16)
        self.painter.drawLine(self.w-self.grid_padding_right+self.percent_width,0,
                            self.w-self.grid_padding_right+self.percent_width,self.h-self.grid_padding_bottom+16)
        self.setPen('yellow')
        self.painter.drawText(self.w-self.grid_padding_right+5,self.h-self.grid_padding_bottom-4,QtCore.QString(u'成交量'))
        self.setPen('white')
        #右下角文字
        self.painter.drawText(self.w-self.grid_padding_right+12,self.h-self.grid_padding_bottom+12,QtCore.QString(u'实时'))

    '''绘制成交量走势表格'''
    def VolumeGridPaint(self):
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        max_volume = self.parent.realtime_data['max_vol']
         
        px_h_radio = max_volume/(grid_height*(1-self.ratio))
         
        self.setPen('red_1px_dashline')
         
        grid_num = 6
        x = grid_num
        cnt = grid_height*(1-self.ratio)/grid_num
        for i in range(0,grid_num):
            self.setPen('red_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+(grid_height*self.ratio)+i*cnt
            x1 = self.grid_padding_left
            x2 = self.grid_padding_left+self.w-sum_width
             
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            vol_int = int(cnt*x*px_h_radio)
            vol_str = str(vol_int)
            fw = self.metrics.width(str(x)) #获得文字宽度
            fh = self.metrics.height()/2   #获得文字高度
            if (i == 0):
                y1 += fh
            self.setPen('white')
            self.painter.drawText(self.w-self.grid_padding_right+self.left_text_padding,y1+fh,str(x)) #写入文字
            self.setPen('yellow')
            self.painter.drawText(x1-2-self.metrics.width(vol_str),y1+fh,vol_str)    #写入文字
            x-=1
         
    def hrightGridPaint(self):
        self.setPen('red')
        #绘制信息内容之间的分割线
        _h = 0
        _x = self.w-self.grid_padding_right+self.percent_width
        self.painter.drawLine(self.w-1,0,self.w-1,self.h-self.grid_padding_bottom+16)
        self.painter.drawLine(0,0,0,self.h-self.grid_padding_bottom+16)
        self.painter.drawLine(0,_h,self.w-1,_h)
        _h+=self.grid_padding_top
        self.painter.drawLine(_x,_h,self.w-1,_h)
        #_h+=24
        #self.painter.drawLine(_x,_h,self.w,_h)
 
        #股票名称和代码
        self.setFont('yahei_14_bold')
        self.setPen('blue')
        stockID_str = QtCore.QString(u'%s'%(self.parent.sid))
        self.painter.drawText(_x+self.left_text_padding*3,self.titley,stockID_str)
        self.setFont('yahei_14_bold')
        self.setPen('yellow')
        fh = self.painter.fontMetrics().height() / 2   #获得文字高度
        y = self.grid_padding_top - fh

        codecName="UTF-8"
        tc = QtCore.QTextCodec.codecForName(codecName)
        QtCore.QTextCodec.setCodecForTr(tc);
        QtCore.QTextCodec.setCodecForCStrings(tc);
        QtCore.QTextCodec.setCodecForLocale(tc);
        name_str = QtCore.QString('%s'%(self.parent.stockname))
        self.painter.drawText(self.w-self.left_text_padding*3-self.painter.fontMetrics().width(name_str),self.titley,name_str)
     #   self.parent.titlew.setTitleData(str(stockID_str), str(name_str))

        # draw current price
        self.setFont('yahei_18_bold')
        current=float(0.0)
        #if (stockID_str[0] == '1' or stockID_str[0] == '5'):
        #    current='%4.3f'%(self.parent.realtime_data['list']['close'][self.parent.datacount-1])
        #else:
        #    current='%4.2f'%(self.parent.realtime_data['list']['close'][self.parent.datacount-1])
        fh = self.painter.fontMetrics().height() 
        #fw = self.painter.fontMetrics().width(current)
        #c = self.parent.realtime_data['list']['close'][self.parent.datacount-1]
        c = str(self.parent.realtime_data['current_price'])
        fw = self.painter.fontMetrics().width(c)
        cy = self.parent.realtime_data['pre_close']
        diff = (float(c)-float(cy))
        percent = '%4.2f'%((diff)*100/cy)
        if (float(c) > float(cy)):
            self.setPen('red')
        else:
            self.setPen('green')
        cprice = float(0.0) 
        if (stockID_str[0] == '1' or stockID_str[0] == '5'):
            cprice = QtCore.QString('%4.3f'%(self.parent.realtime_data['current_price']))
            cp = QtCore.QString('%4.3f'%(self.parent.realtime_data['pre_close']))
            diff = '%4.3f'%diff
        else:
            cprice = QtCore.QString('%4.2f'%(self.parent.realtime_data['current_price']))
            cp = QtCore.QString('%4.2f'%(self.parent.realtime_data['pre_close']))
            diff = '%4.2f'%diff

        # draw current price
#        for diameter in range(0, 256, 9):
#            delta = abs((self.parent.frameNo % 128) - diameter / 2)
#            alpha = 255 - (delta * delta) / 4 - diameter
#            print "alpha = ",alpha
#            if alpha > 0:
        if (float(c)>float(cy)):
            #self.painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, alpha), 3))
            self.setPen('red')
            #self.painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, 255), 3))
        else:
            #self.painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, alpha), 3))
            #self.painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, 255), 3))
            self.setPen('green')

        if self.parent.floatBased:
            if (float(cprice)==0):
                cprice = str(cp)
                fw = self.painter.fontMetrics().width(cprice)
                self.painter.drawText(_x+(self.grid_padding_right-self.percent_width-fw)/2,54,cprice)
                cprice = float(0.0)
            else:
                self.painter.drawText(_x+(self.grid_padding_right-self.percent_width-fw)/2,54,cprice)
        else:
            if (float(cprice)==0):
                cprice = str(cp)
                fw = self.painter.fontMetrics().width(cprice)
                self.painter.drawText(_x+(self.grid_padding_right-self.percent_width-fw)/2,54,cprice)
                cprice = float(0.0)
            else:
                self.painter.drawText(_x+(self.grid_padding_right-self.percent_width-fw)/2,54,cprice)


        #self.painter.drawText(_x+(self.grid_padding_right-fw)/2,54,str(self.parent.realtime_data['list']['close'][self.parent.datacount-1]))
        ss=""
        if (float(c) > float(cy)):
            ss='%s  +%s'%(str(diff),str(percent))
        else:
            ss='%s  %s'%(str(diff),str(percent))
        ss += "%"
        self.setFont('yahei_14_bold')
        fw=self.painter.fontMetrics().width(ss)
        _h = 50 + fh*0.8
        #for diameter in range(0, 256, 9):
        #    delta = abs((self.parent.frameNo % 128) - diameter / 2)
        #    alpha = 255 - (delta * delta) / 4 - diameter
        #    if alpha > 0:
        if (float(c)>float(cy)):
            #self.painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, 255), 3))
            self.setPen('red')
        else:
            #self.painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, 255), 3))
            self.setPen('green')

        if self.parent.floatBased:
            if (float(cprice)==0):
                ss = "停牌"
                fw=self.painter.fontMetrics().width(ss)
            self.painter.drawText(_x + (self.grid_padding_right-self.percent_width-fw)/2, _h, ss)
        else:
            if (float(cprice)==0):
                ss = "停牌"
                fw=self.painter.fontMetrics().width(ss)
            self.painter.drawText(_x + (self.grid_padding_right-self.percent_width-fw)/2, _h, ss)
        flag = int(0)
        if (float(c) > float(cy)):
            flag = int(1)
        else:
            flag = int(0)
#        self.parent.flashw.setFwData(str(cprice), str(ss), int(flag))

        self.setPen('red')
        self.setFont('yahei_14_bold')
        _h = 50 + fh*1.1
        self.painter.drawLine(self.w-self.grid_padding_right+self.percent_width, _h, self.w-1, _h)
        _sh = _h-22
        _yh = _h
        _yh+=118
        self.painter.drawLine(_x,_yh,self.w-1,_yh)
        _yh+=21
        self.painter.drawLine(_x,_yh,self.w-1,_yh)
        _yh+=93
        self.painter.drawLine(_x,_yh,self.w-1,_yh)
        _yh+=123
        self.painter.drawLine(_x,_yh,self.w-1,_yh)
        _yh+=23
        self.painter.drawLine(_x,_yh,self.w-1,_yh)

#        name_str = QtCore.QString(u'%s %s'%(self.parent.sid,self.parent.stk_info['name']))
#        ss=QtCore.QString(u'%s %s'%

            #price = '%4.2f'%(price_float) #格式化价格成str
        #name_str = QtCore.QString(u'%s'%(self.parent.sid))
         
        #委比和委差
        self.setPen('gray')
        self.setFont('yahei_14_bold')
        zx_str = QtCore.QString(u'最新')
        _xh = 218
        self.setPen('gray')
        self.painter.drawText(_x+self.left_text_padding,_xh,zx_str)
        _h += 17
        wb_str = QtCore.QString(u'委比')
        wc_str = QtCore.QString(u'委差')
        self.setPen('red')
        self.painter.drawLine(_x, _h+6, self.w-1, _h+6)
        xs_str = QtCore.QString(u'现手')
        self.setPen('gray')
        self.painter.drawText(_x+self.left_text_padding, _h , wb_str)
        self.painter.drawText(_x+100, _h, wc_str)
        self.painter.drawText(_x+100,_xh,xs_str)
        fh = self.painter.fontMetrics().height()
        _h += 5
         
        left_field_list = [u'涨跌',u'涨幅',u'振幅',u'总手',u'总额',u'换手',u'分笔']
        i = 1
        for field in left_field_list:
            field_str = QtCore.QString(field)
            self.painter.drawText(_x+self.left_text_padding,253+(i*17)+_sh,field_str)
            i+=1
 
        right_field_list = [u'均价',u'前收',u'今开',u'最高',u'最低',u'量比',u'均量']

        c = self.parent.realtime_data['current_price']
        cy = self.parent.realtime_data['pre_close']
         
        i = 1
        for field in right_field_list:
            field_str = QtCore.QString(field)
            self.painter.drawText(_x+100,253+(i*17)+_sh,field_str)
            i+=1
 
        wp_str = QtCore.QString(u'外盘')
        np_str = QtCore.QString(u'内盘')
        self.painter.drawText(_x + self.left_text_padding, 393 + _sh, wp_str)

        self.setFont('yahei_14_normal')
        for i in range(0, len(ticks_today['time'])):
            if (418 + (i * 17) + fh + _sh) > (self.h - self.grid_padding_bottom):
                break
            tickStr = ""
            tickStr += str(ticks_today['time'][i])
            tickStr += " "
            tickStr += str(ticks_today['price'][i])
            tickStr += " "
            types = str(ticks_today['type'][i])
            vtypes = str(ticks_today['volume'][i])
            vtypes += " "
            if (types == '中性盘'):
                vtypes += "中性"
            else:
                vtypes += str(ticks_today['type'][i])
            fw = self.painter.fontMetrics().width(vtypes)

            self.setPen('gray')
            self.painter.drawText(_x + self.left_text_padding, 418 + (i * 17) + _sh, tickStr)

            if (types == '买盘'):
                self.setPen('red')
            else:
                self.setPen('green')
            self.painter.drawText(self.w - fw - self.left_text_padding, 418 + (i * 17) + _sh, vtypes)

        self.setFont('yahei_14_bold')
        self.setPen('gray')
        self.painter.drawText(_x+100,393+_sh,np_str)
        #卖①②③④⑤
         
        i = 0
        sell_queue = [u'卖⑤',u'卖④',u'卖③',u'卖②',u'卖①']
        for sell in sell_queue:
            sell_str = QtCore.QString(sell)
            self.setPen('gray')
            self.painter.drawText(_x + self.left_text_padding, 62 + (i*18) + _sh, sell_str)
            if (c > cy):
                self.setPen('red')
            else:
                self.setPen('green')
            self.painter.drawText(_x + self.left_text_padding + self.metrics.width(str(sell_str))*1.5, 62 + (i*18) + _sh, QtCore.QString(str(self.parent.realtime_data['list']['sell_p'][4-i])))
            self.setPen('yellow')
            vstring = QtCore.QString(str(self.parent.realtime_data['list']['sell_v'][4-i]))
            self.painter.drawText(self.w - self.left_text_padding*2 - self.metrics.width(str(vstring)) - 3, 62 + (i*18) + _sh, vstring)
            i += 1

        self.setPen('gray')
        #买①②③④⑤
        buy_queue = [u'买①',u'买②',u'买③',u'买④',u'买⑤']
        f = 0
        for buy in buy_queue:
            buy_str = QtCore.QString(buy)
            self.setPen('gray')
            self.painter.drawText(_x + self.left_text_padding, 87 + (i*18) + _sh, buy_str)
            if (c > cy):
                self.setPen('red')
            else:
                self.setPen('green')
            self.painter.drawText(_x + self.left_text_padding + self.metrics.width(str(buy_str))*1.5, 87 + (i*18) + _sh, QtCore.QString(str(self.parent.realtime_data['list']['buy_p'][f])))
            self.setPen('yellow')
            vstring = QtCore.QString(str(self.parent.realtime_data['list']['buy_v'][f]))
            self.painter.drawText(self.w - self.left_text_padding*2 - self.metrics.width(str(vstring)) - 3, 87 + (i*18) + _sh, vstring)
            f += 1
            i += 1
 
        self.setPen('red')
        #self.setPen('red_2px')
        self.painter.drawLine(_x+1,377+_sh,_x+99,377+_sh)
        #self.painter.drawLine(_x+1,46,_x+65,46)
        #self.setPen('green_2px')
        self.painter.drawLine(_x+102,377+_sh,_x+199,377+_sh)
        #self.painter.drawLine(_x+67,46,_x+199,46)
        self.setFont('default')

    '''绘制左侧信息栏和盘口等内容'''
    def rightGridPaint(self):
        self.setPen('red')
        #绘制信息内容之间的分割线
        _h = 0
        _x = self.w-self.grid_padding_right+self.percent_width
        self.painter.drawLine(self.w-1,0,self.w-1,self.h-self.grid_padding_bottom+16)
        self.painter.drawLine(0,0,0,self.h-self.grid_padding_bottom+16)
        self.painter.drawLine(0,_h,self.w,_h)
        _h+=23
        self.painter.drawLine(_x,_h,self.w,_h)
        _h+=24
        self.painter.drawLine(_x,_h,self.w,_h)
 
        _h+=93
        self.painter.drawLine(_x,_h,self.w,_h)
        _h+=20
        self.painter.drawLine(_x,_h,self.w,_h)
        _h+=93
        self.painter.drawLine(_x,_h,self.w,_h)
        _h+=123
        self.painter.drawLine(_x,_h,self.w,_h)
        _h+=23
        self.painter.drawLine(_x,_h,self.w,_h)
        #股票名称和代码
        self.setFont('yahei_14_bold')
        self.setPen('blue')
        code = QtCore.QString(u'%s'%(self.parent.sid))
        self.painter.drawText(_x+self.left_text_padding*3,18,code)
        name_str = QtCore.QString(u'%s'%(self.parent.stockname))
        self.setPen('yellow')
        self.painter.drawText(self.w-self.left_text_padding*3-self.painter.fontMetrics().width(name_str),18,name_str)
        #委比和委差
        self.setFont('yahei_14')
        zx_str = QtCore.QString(u'最新')
        self.painter.drawText(_x+3,156,zx_str)
        self.setPen('gray')
        wb_str = QtCore.QString(u'委比')
        wc_str = QtCore.QString(u'委差')
        xs_str = QtCore.QString(u'现手')
        self.painter.drawText(_x+3  ,39,wb_str)
        self.painter.drawText(_x+100,39,wc_str)
        self.painter.drawText(_x+100,156,xs_str)
        fh = self.metrics.height()
         
        left_field_list = [u'涨跌',u'涨幅',u'振幅',u'总手',u'总额',u'换手',u'分笔']
        i = 1
        for field in left_field_list:
            field_str = QtCore.QString(field)
            self.painter.drawText(_x+3,253+(i*17),field_str)
            i+=1
 
        right_field_list = [u'均价',u'前收',u'今开',u'最高',u'最低',u'量比',u'均量']
         
        i = 1
        for field in right_field_list:
            field_str = QtCore.QString(field)
            self.painter.drawText(_x+100,253+(i*17),field_str)
            i+=1
 
        wp_str = QtCore.QString(u'外盘')
        np_str = QtCore.QString(u'内盘')
        self.painter.drawText(_x+3,395,wp_str)
        self.painter.drawText(_x+100,395,np_str)
        #卖①②③④⑤
         
        i = 0
        sell_queue = [u'卖⑤',u'卖④',u'卖③',u'卖②',u'卖①']
        for sell in sell_queue:
            sell_str = QtCore.QString(sell)
            self.painter.drawText(_x+3,62+(i*18),sell_str)
            i+=1

        #买①②③④⑤
        buy_queue = [u'买①',u'买②',u'买③',u'买④',u'买⑤']
        for buy in buy_queue:
            buy_str = QtCore.QString(buy)
            self.painter.drawText(_x+3,87+(i*18),buy_str)
            i+=1
 
        self.setPen('red_2px')
        self.painter.drawLine(_x+1,377,_x+99,377)
        self.painter.drawLine(_x+1,46,_x+65,46)
        self.setPen('green_2px')
        self.painter.drawLine(_x+102,377,_x+199,377)
        self.painter.drawLine(_x+67,46,_x+199,46)
        self.setFont('default')
         
    '''绘制左右侧的价格刻度'''
    def rulerPaint(self):
         
        sum_width  = self.grid_padding_left+self.grid_padding_right
        sum_height = self.grid_padding_top+self.grid_padding_bottom
 
        grid_height = self.h-sum_height
         
        high = self.parent.realtime_data['high']
        low  = self.parent.realtime_data['low']
        pre_close = self.parent.realtime_data['pre_close']
 
        top = high-pre_close
        bottom = pre_close-low
        if top>bottom:
            padding = top
        else:
            padding = bottom
             
        limit_top = pre_close+padding
        limit_low = pre_close-padding
 
        px_h_radio = 0.0
        if (limit_top != limit_low):
            px_h_radio = (grid_height*self.ratio)/((limit_top-limit_low)*100)
         
        self.setPen('red_1px_dashline')
 
        grid_num = 16
        cnt = grid_height*self.ratio/grid_num
         
        for i in range(0,grid_num):
            self.setPen('red_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+i*cnt
            x1 = self.grid_padding_left
            x2 = self.grid_padding_left+self.w-sum_width
             
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            price_float = (limit_top - ((i*cnt)/px_h_radio/100)) #计算价格
            price = '%4.2f'%(price_float) #格式化价格成str
             
            fw = self.metrics.width(price) #获得文字宽度
            fh = self.metrics.height()/2   #获得文字高度
 
            radio_float = (price_float/pre_close-1)*100 #计算波动百分比
            radio_str   = "%2.2f%%"%(radio_float)      #格式化百分比成str
 
            r_fw = self.metrics.width(radio_str)
            r_fh = self.metrics.height()/2
            #判断文字使用的颜色
            if price_float == pre_close:
                self.setPen('white')
            if price_float < pre_close:
                self.setPen('green')
                 
            if (self.parent.current_moving_item == -1) and (not (self.parent.m_x >= self.grid_padding_left and self.parent.m_x<=self.w-self.grid_padding_right and self.parent.m_y>=self.grid_padding_top and self.parent.m_y<=self.h-self.grid_padding_bottom)):

                self.painter.drawText(x1-fw-2,y1+fh,price) #写入文字
            self.painter.drawText(self.w-self.grid_padding_right+self.left_text_padding,y1+r_fh,radio_str) #写入文字
        if (self.parent.current_moving_item == -1) and (not (self.parent.m_x >= self.grid_padding_left and self.parent.m_x<=self.w-self.grid_padding_right and self.parent.m_y>=self.grid_padding_top and self.parent.m_y<=self.h-self.grid_padding_bottom)):
            self.painter.drawText(self.grid_padding_left-self.metrics.width('%4.2f'%(low))-2,self.grid_padding_top+grid_height*self.ratio,'%4.2f'%(low))  
        rp = str('%2.2f'%((low-pre_close)*100/pre_close))
        rp += "%"
        self.painter.drawText(self.w-self.grid_padding_right+self.left_text_padding,self.grid_padding_top+grid_height*self.ratio,rp) #写入文字

    '''绘制x,y准星'''
    def xyPaint(self):
        if self.parent.datacount < 1:return
        if ((self.parent.current_moving_item != -1) and (not (self.parent.m_x >= self.grid_padding_left and self.parent.m_x<=self.w-self.grid_padding_right and self.parent.m_y>=self.grid_padding_top and self.parent.m_y<=self.h-self.grid_padding_bottom))):
            self.setPen('gray')
            x1 = self.grid_padding_left
            x2 = self.w-self.grid_padding_right
            y1 = self.grid_padding_top
            y2 = self.h-self.grid_padding_bottom
        #    self.painter.drawLine(x1+1,self.parent.m_y,x2-1,self.parent.m_y)
            sum_width  = self.grid_padding_left+self.grid_padding_right
            sum_height = self.grid_padding_top+self.grid_padding_bottom
 
            grid_height = self.h-sum_height-2
             
            high = self.parent.realtime_data['high']
            low  = self.parent.realtime_data['low']
            pre_close = self.parent.realtime_data['pre_close']
 
            top = high-pre_close
            bottom = pre_close-low
            if top>bottom:
                padding = top
            else:
                padding = bottom
                 
            limit_top = pre_close+padding
            limit_low = pre_close-padding
             
            h_radio = (grid_height*self.ratio)/((limit_top-limit_low)*100)
 
            w_radio = (self.w-sum_width-2)/self.totalRealtimeK

            index = int(self.parent.current_moving_item)+1
            
            mx = (index*w_radio+self.grid_padding_left)

            #draw square
            self.setPen('magan')
            self.painter.drawRect(2,self.grid_padding_top,self.grid_padding_left-4, grid_height*self.ratio)
             
            self.setPen('gray')
            self.painter.drawLine(mx,y1+1,mx,y2-1)

            print "index =",index
            time = str(conf.TimeStamps[index-1]) 
            price = float(0.0)
            vol = int(0) 
            try:
                ind = self.parent.realtime_data['list']['time'].index(time) 
                price = self.parent.realtime_data['list']['close'][ind]
                vol = self.parent.realtime_data['list']['vol'][ind]
            except Exception as e:
                pass

            y = self.grid_padding_top + 8
            index = self.parent.current_moving_item
            label = '时   间'
            fw = self.painter.fontMetrics().width(label)
            fh = self.painter.fontMetrics().height() 
            self.setPen('white')
            self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
            str1 = str(time)
            fw = self.painter.fontMetrics().width(str1)
            self.setPen('yellow')
            self.painter.drawText((self.grid_padding_left - fw -4), y + 18 +self.grid_padding_top - (fh / 2.0), str1)
            y += 22 
            y += fh
            label = "当前价" 
            fw = self.painter.fontMetrics().width(label)
            fh = self.painter.fontMetrics().height() 
            self.setPen('white')
            self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
                
            if (self.parent.sid[0] == '1' or self.parent.sid[0] == '5'):#ETF 
                str1 = '%.3f'%(float(price))
            else:
                str1 = '%.2f'%(float(price))
            fw = self.painter.fontMetrics().width(str1)
            self.setPen('yellow')
            self.painter.drawText((self.grid_padding_left - fw -4), y + 18 + self.grid_padding_top - (fh / 2.0), str1)
            y += 22
            y += fh
            label = "成交量" 
            fw = self.painter.fontMetrics().width(label)
            fh = self.painter.fontMetrics().height() 
            self.setPen('white')
            self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
            str1 = str(vol)
            fw = self.painter.fontMetrics().width(str1)
            self.setPen('yellow')
            self.painter.drawText((self.grid_padding_left - fw -4), y + 18 + self.grid_padding_top - (fh / 2.0), str1)

            #self.painter.drawLine(self.parent.m_x,y1+1,self.parent.m_x,y2-1)
        elif self.parent.m_x >= self.grid_padding_left and self.parent.m_x<=self.w-self.grid_padding_right and self.parent.m_y>=self.grid_padding_top and self.parent.m_y<=self.h-self.grid_padding_bottom:
            self.setPen('gray')
            x1 = self.grid_padding_left
            x2 = self.w-self.grid_padding_right
            y1 = self.grid_padding_top
            y2 = self.h-self.grid_padding_bottom
        #    self.painter.drawLine(x1+1,self.parent.m_y,x2-1,self.parent.m_y)
            sum_width  = self.grid_padding_left+self.grid_padding_right
            sum_height = self.grid_padding_top+self.grid_padding_bottom
 
            grid_height = self.h-sum_height-2
             
            high = self.parent.realtime_data['high']
            low  = self.parent.realtime_data['low']
            pre_close = self.parent.realtime_data['pre_close']
 
            top = high-pre_close
            bottom = pre_close-low
            if top>bottom:
                padding = top
            else:
                padding = bottom
                 
            limit_top = pre_close+padding
            limit_low = pre_close-padding
             
            h_radio = (grid_height*self.ratio)/((limit_top-limit_low)*100)
 
            w_radio = (self.w-sum_width-2)/self.totalRealtimeK

            index = int((self.parent.m_x - self.grid_padding_left)/w_radio)+1
            
            mx = (index*w_radio+self.grid_padding_left)
             
            tl = self.parent.realtime_data['list']['time'][self.parent.datacount-1]
#            print "tl = ", tl, "last time = ", conf.TimeStamps.index(tl)
#            print "index = ",index
            jj = get_trading_time_index()
            if (jj < 0 or (index-1) > jj): return #conf.TimeStamps.index(tl)): return

            self.painter.drawLine(mx,y1+1,mx,y2-1)
            #self.painter.drawLine(self.parent.m_x,y1+1,self.parent.m_x,y2-1)
            #draw square
            self.setPen('magan')
            self.painter.drawRect(2,self.grid_padding_top,self.grid_padding_left-4, grid_height*self.ratio)
            time = str(conf.TimeStamps[index-1]) 
            price = float(0.0)
            vol = int(0) 
            try:
                ind = self.parent.realtime_data['list']['time'].index(time) 
                price = self.parent.realtime_data['list']['close'][ind]
                vol = self.parent.realtime_data['list']['vol'][ind]
            except Exception as e:
                pass

            y = self.grid_padding_top + 8
            index = self.parent.current_moving_item
            label = '时   间'
            fw = self.painter.fontMetrics().width(label)
            fh = self.painter.fontMetrics().height() 
            self.setPen('white')
            self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
            str1 = str(time)
            fw = self.painter.fontMetrics().width(str1)
            self.setPen('yellow')
            self.painter.drawText((self.grid_padding_left - fw -4), y + 18 +self.grid_padding_top - (fh / 2.0), str1)
            y += 22 
            y += fh
            label = "当前价" 
            fw = self.painter.fontMetrics().width(label)
            fh = self.painter.fontMetrics().height() 
            self.setPen('white')
            self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
                
            if (self.parent.sid[0] == '1' or self.parent.sid[0] == '5'):#ETF 
                str1 = '%.3f'%(float(price))
            else:
                str1 = '%.2f'%(float(price))
            fw = self.painter.fontMetrics().width(str1)
            self.setPen('yellow')
            self.painter.drawText((self.grid_padding_left - fw -4), y + 18 + self.grid_padding_top - (fh / 2.0), str1)
            y += 22
            y += fh
            label = "成交量" 
            fw = self.painter.fontMetrics().width(label)
            fh = self.painter.fontMetrics().height() 
            self.setPen('white')
            self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
            str1 = str(vol)
            fw = self.painter.fontMetrics().width(str1)
            self.setPen('yellow')
            self.painter.drawText((self.grid_padding_left - fw -4), y + 18 + self.grid_padding_top - (fh / 2.0), str1)
#        else# (not (self.parent.m_x >= self.grid_padding_left and self.parent.m_x<=self.w-self.grid_padding_right and self.parent.m_y>=self.grid_padding_top and self.parent.m_y<=self.h-self.grid_padding_bottom)):
#            self.parent.current_moving_item = -1

     
    '''绘制时间轴刻度'''
    def timelinePaint(self):
         
        fw = self.metrics.width(u'00:00') #计算文字的宽度
         
        sum_width  = self.grid_padding_left+self.grid_padding_right
        sum_height = self.grid_padding_top+self.grid_padding_bottom
         
        grid_width = self.w-sum_width-2
         
        y1 = self.grid_padding_top
        y2 = y1+(self.h-sum_height)
 
        #时间轴中线
        self.setPen('red')
        x_pos = grid_width/2+self.grid_padding_left
         
        self.painter.drawLine(x_pos,y1,x_pos,y2)
        self.setPen('white')
        self.painter.drawText(15+x_pos-self.metrics.width(u'11:30/13:00')/2,y2+12,QtCore.QString(u'11:30/13:00'))
         
        #时间轴09点30分
        x_pos = self.grid_padding_left
        self.painter.drawText(x_pos,y2+12,QtCore.QString(u'09:30'))
         
        #时间轴10点30分
        x_pos = grid_width*0.25+self.grid_padding_left
        self.setPen('red')
        self.painter.drawLine(x_pos,y1,x_pos,y2)
        self.setPen('white')
        self.painter.drawText(10+x_pos-fw/2,y2+12,QtCore.QString(u'10:30'))
 
        #时间轴14点00分
        x_pos = grid_width*0.75+self.grid_padding_left
        self.setPen('red')
        self.painter.drawLine(x_pos,y1,x_pos,y2)
        self.setPen('white')
        self.painter.drawText(10+x_pos-fw/2,y2+12,QtCore.QString(u'14:00'))
 
        #时间轴15点00分
        x_pos = grid_width+self.grid_padding_left
        #self.painter.drawText(15+x_pos-fw,y2+12,QtCore.QString(u'15:00'))
        self.painter.drawText(self.w-self.grid_padding_right-self.metrics.width(u'15:00'),y2+12,QtCore.QString(u'15:00'))
 
        #时间虚线 by 30min
        self.setPen('red_1px_dashline')
        x_pos_array = [0.125,0.375,0.625,0.875]
        for i in x_pos_array:
            x_pos = grid_width*i+self.grid_padding_left
            self.painter.drawLine(x_pos,y1,x_pos,y2)
         
    '''绘制表格上方的股票信息'''
    def topInfoPaint(self):
        self.setPen('yellow')
        self.painter.drawText(4+self.grid_padding_left,self.grid_padding_top-4
                            ,QtCore.QString((self.parent.stockname))) #股票名称
        self.painter.drawText(4+self.grid_padding_left+120,self.grid_padding_top-4
                            ,QtCore.QString(u'均价线：')) #均价线
        pre_close = self.parent.realtime_data['pre_close']
        close     = self.parent.realtime_data['close']
        mma = 0.0
        if (self.parent.datacount>0):
            mma = self.parent.realtime_data['list']['mma'][-1]
         
        if pre_close>close:
            self.setPen('green')
            str_1 = '%.2f -%.2f'%(close,pre_close-close)
        if pre_close==close:
            self.setPen('white')
            str_1 = '%.2f +%.2f'%(close,0.00)
        if pre_close<close:
            self.setPen('red')
            str_1 = '%.2f +%.2f'%(close,close-pre_close)
         
        if mma>close:
            self.setPen('green')
        if mma==close:
            self.setPen('white')
        if mma<close:
            self.setPen('red')
         
        self.painter.drawText(4+self.grid_padding_left+55,self.grid_padding_top-4,QtCore.QString(str_1))
        self.painter.drawText(4+self.grid_padding_left+165,self.grid_padding_top-4,QtCore.QString('%.2f'%mma)) #均价
         
        #涨停价
        self.setPen('red')
        self.painter.drawText(4+self.grid_padding_left+200,self.grid_padding_top-4,QtCore.QString(u'涨停价:%.2f'%(pre_close*1.1))) #均价
        #跌停价
        self.setPen('green')
        self.painter.drawText(4+self.grid_padding_left+280,self.grid_padding_top-4,QtCore.QString(u'跌停价:%.2f'%(pre_close*0.9))) #均价

    '''绘制股价走势'''
    def pricePaint(self):
        sum_width  = self.grid_padding_left+self.grid_padding_right
        sum_height = self.grid_padding_top+self.grid_padding_bottom
 
        grid_height = self.h-sum_height-2
         
        high = self.parent.realtime_data['high']
        low  = self.parent.realtime_data['low']
        pre_close = self.parent.realtime_data['pre_close']
 
        top = high-pre_close
        bottom = pre_close-low
        if top>bottom:
            padding = top
        else:
            padding = bottom
             
        limit_top = pre_close+padding
        limit_low = pre_close-padding
         
        h_radio = (grid_height*self.ratio)/((limit_top-limit_low)*100)
 
        w_radio = (self.w-sum_width-2)/self.totalRealtimeK
        w = self.grid_padding_left
         
        self.setPen('white')
        #self.painter.setBrush(QtCore.Qt.yellow)
        path = QtGui.QPainterPath()
        path.moveTo(w,(limit_top-self.parent.realtime_data['open'])*100*h_radio+self.grid_padding_top)

        for time in conf.TimeStamps:
            index = conf.TimeStamps.index(time)+1
            try:
                idx = self.parent.realtime_data['list']['time'].index(time)
                price = self.parent.realtime_data['list']['close'][idx]
                w = index*w_radio+self.grid_padding_left
                y = (limit_top-price)*100*h_radio+self.grid_padding_top
                path.lineTo(w,y)
            except Exception as e:
                pass
        #i  = 1
        #for price in self.parent.realtime_data['list']['close']:
        #    w = i*w_radio+self.grid_padding_left
        #    y = (limit_top-price)*100*h_radio+self.grid_padding_top
        #    path.lineTo(w,y)
        #    i+=1
        self.painter.drawPath(path)
        self.setPen('cyan_1px_dashline')
#        self.painter.drawLine(self.grid_padding_left+1,y,w-1,y)
        self.setPen('yellow')
        path = QtGui.QPainterPath()
        w = self.grid_padding_left
        path.moveTo(w,(limit_top-self.parent.realtime_data['open'])*100*h_radio+self.grid_padding_top)
        for time in conf.TimeStamps:
        #for time in self.parent.realtime_data['list']['time']:
            index = conf.TimeStamps.index(time)+1
            try:
                idx = self.parent.realtime_data['list']['time'].index(time)
                mma = self.parent.realtime_data['list']['mma'][idx]
                w = index*w_radio+self.grid_padding_left
                y = (limit_top-mma)*100*h_radio+self.grid_padding_top
                path.lineTo(w,y)
            except Exception as e:
                pass
        #i  = 1
        #for price in self.parent.realtime_data['list']['mma']:
        #    w = i*w_radio+self.grid_padding_left
        #    y = (limit_top-price)*100*h_radio+self.grid_padding_top
        #    path.lineTo(w,y)
        #    i+=1
        self.painter.drawPath(path)
         
         
    '''绘制成交量'''
    def volumePaint(self):
        if (self.parent.datacount <= 0):
            return
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        max_volume = self.parent.realtime_data['max_vol'] #最大分钟成交量
         
        w_radio = (self.w-sum_width-2)/self.totalRealtimeK
        h_radio = ((self.h-sum_height-8)*(1-self.ratio))/max_volume
 
        y = (self.h-sum_height)+self.grid_padding_top
         
        self.setPen('yellow')
             
        for time in conf.TimeStamps:
            try:
                idx = self.parent.realtime_data['list']['time'].index(time)
                index = conf.TimeStamps.index(time)+1
                x = index*w_radio+self.grid_padding_left
                y2 = h_radio*self.parent.realtime_data['list']['vol'][idx]
                if int(self.parent.realtime_data['list']['type'][idx]) == 1:
                    self.setPen('yellow')
                else:
                    self.setPen('green')
                    #self.setPen('yellow')
                self.painter.drawLine(x,y-1,x,y-y2)
            except Exception as e:
                pass
 
class RealtimeQuotesWidget(QtGui.QWidget):
    def __init__(self, stockid, parent=None):
        QtGui.QWidget.__init__(self, parent)
        self.sid = stockid
        self.spath=stockid
        self.spath += "/"
        self.swidth = QApplication.desktop().availableGeometry().width()
        self.sheight = QApplication.desktop().availableGeometry().height()
        print "www = ", self.swidth, " hhh = ", self.sheight
        self.setMinimumSize(640, 430) #设置窗口最小尺寸
        self.setGeometry(0, 0, self.swidth, self.sheight)
        self.setMinimumSize(640, 430) #设置窗口最小尺寸
        self.setWindowTitle(QtCore.QString(u'行情实时走势'))
        self.setStyleSheet("QWidget { background-color: black }")
        self.setWindowIcon(QtGui.QIcon('ruby.png'))
        self.setMouseTracking(True)
        self.setCursor(Qt.CrossCursor)

        codecName="UTF-8"
        tc = QtCore.QTextCodec.codecForName(codecName)
        QtCore.QTextCodec.setCodecForTr(tc);
        QtCore.QTextCodec.setCodecForCStrings(tc);
        QtCore.QTextCodec.setCodecForLocale(tc);

        self.m_x = 0 #光标x轴位置
        self.m_y = 0 #光标y轴位置

        self.current_moving_item = -1

        self.seq = 0
        self.high = 0.0
        self.low = 0.0
        self.frameNo = 0
        self.floatBased = False

        self.realtime_data = {}
        self.realtime_data['list'] = {} #股价序列
        self.realtime_data['list']['time']  = [] #时间
        self.realtime_data['list']['close'] = [] #收盘价
        self.realtime_data['list']['vol']   = [] #成交量
        self.realtime_data['list']['amount']= [] #成交额
        self.realtime_data['list']['mma']   = []   #分时均价
        self.realtime_data['list']['type']   = []   #分时均价

        self.realtime_data['pre_close'] = 0.0 #上一个交易日收盘价
        self.realtime_data['open'] = 0.0       #开盘价
        self.realtime_data['high'] = 0.0 
        self.realtime_data['low']  = 0.0 
        self.realtime_data['close']= 0.0 
        self.realtime_data['max_vol']  = 0.0 
        self.realtime_data['current_price'] = 0.0 
        self.datacount = 0
         
        self.realtime_data['list']['buy_port'] = [(0.00,0),(0.00,0),(0.00,0),(0.00,0),(0.00,0)]  #买盘前五
        self.realtime_data['list']['sell_port'] = [(0.00,0),(0.00,0),(0.00,0),(0.00,0),(0.00,0)] #卖盘前五

        self.realtime_data['list']['buy_v']  = []  #买盘前五
        self.realtime_data['list']['buy_p']  = []  #买盘前五

        self.realtime_data['list']['sell_v'] = []  #买盘前五
        self.realtime_data['list']['sell_p'] = []  #买盘前五

        if not os.path.exists(self.spath+"realtime"):
            indicators.get_realtime(self.sid)

        self.load_realtime_data()

        self.load_ticks()
         
        mycode = locale.getpreferredencoding()

        self.tick_thread = Ticks_Reading_Thread(self.sid)
        self.tick_thread.rendered_today_ticks.connect(self.today_ticks_update)
        self.tick_thread.start()

        self.pankou_thread = Pankou_Reading_Thread(self.sid)
        self.pankou_thread.rendered_realtime.connect(self.realtime_update)
        self.pankou_thread.start()

    def terminal(self):
        while ((not self.tick_thread.isFinished())):
            self.tick_thread.quit()
            self.tick_thread.terminate()
            self.tick_thread.exit(0)

        while ((not self.pankou_thread.isFinished())):
            self.pankou_thread.quit()
            self.pankou_thread.terminate()
            self.pankou_thread.exit(0)

    def closeEvent(self, e):
        self.terminal()
        e.accept()
        sys.exit()

    def keyPressEvent(self, event):
        key = event.key()
        iff = get_trading_time_index()
        print "iff = ",iff
        if key == QtCore.Qt.Key_Left or key == QtCore.Qt.Key_H:
            self.m_x = 0
            self.m_y = 0
            if (self.current_moving_item == -1 and iff > 0):
                #self.current_moving_item = len(conf.TimeStamps)-1
                self.current_moving_item = iff 
            elif (self.current_moving_item > 0):
                self.current_moving_item -= 1 
            self.update()
        elif key == QtCore.Qt.Key_Escape or key == QtCore.Qt.Key_Down or key == QtCore.Qt.Key_Up:
            self.m_x = 0
            self.m_y = 0
            self.current_moving_item = -1
            self.update()
        elif key == QtCore.Qt.Key_Right or key == QtCore.Qt.Key_L:
            self.m_x = 0
            self.m_y = 0
            if (self.current_moving_item == -1):
                self.current_moving_item = 0 
            elif (iff > 0 and self.current_moving_item < iff):
                self.current_moving_item += 1 
            self.update()
        else:
            super(KHistoryWidget, self).keyPressEvent(event)

    def realtime_update(self, text):
        print self.seq, "realtime_update", text
        self.load_realtime_data()
        self.update()
         
    def today_ticks_update(self, text):
        print self.seq, "today_ticks_update", text
        self.seq += 1
        self.load_realtime_data()
        self.load_ticks()
        self.update()

    def load_ticks(self):
        #读取数据
        ss = self.sid
        ss += "/1_min"
        if not os.path.exists(ss):
            return
        f = open(ss,'r')
        data = f.readlines()
        f.close()
         
        for row in data:
            vars = row.split(',')
            try:
                index = self.realtime_data['list']['time'].index(vars[0])
                self.realtime_data['list']['time'][index]   = (vars[0])
                self.realtime_data['list']['close'][index]  = (float(vars[1]))
                self.realtime_data['list']['vol'][index]    = (int(float(vars[2])))
                self.realtime_data['list']['amount'][index] = (int(float(vars[3])))
                self.realtime_data['list']['mma'][index]    = (float(vars[4]))
                self.realtime_data['list']['type'][index]    = (int(vars[5]))
            except: 
                self.realtime_data['list']['time'].append(vars[0])
                self.realtime_data['list']['close'].append(float(vars[1]))
                self.realtime_data['list']['vol'].append(int(float(vars[2])))
                self.realtime_data['list']['amount'].append(int(float(vars[3])))
                sum_vol = sum(self.realtime_data['list']['vol'])*100
                sum_amt = sum(self.realtime_data['list']['amount'])
                self.realtime_data['list']['mma'].append(float(vars[4]))
                self.realtime_data['list']['type'].append(int(vars[5]))
             
        if len(self.realtime_data['list']['time'])>0:
            self.realtime_data['high'] = max(max(self.realtime_data['list']['close']),self.high)     #最高价
            self.realtime_data['low']  = min(min(self.realtime_data['list']['close']),self.low)      #最低价
            self.realtime_data['close']= self.realtime_data['list']['close'][-1]     #收盘价
            self.realtime_data['max_vol']  = max(self.realtime_data['list']['vol'])  #当日最高成交量

        self.datacount = len(self.realtime_data['list']['time'])


    def load_realtime_data(self):
        if not os.path.exists(self.spath+"realtime"):
            return
        f = open(self.spath + "realtime", 'r')
        pankou = f.readlines()
        f.close()
        for row in pankou:
            vars = row.split(',')
            tt = vars[0].split('"')
            self.stockname = str(tt[1])
            self.realtime_data['open'] = float(vars[1])
            self.realtime_data['pre_close'] = float(vars[2])
            self.realtime_data['current_price'] = float(vars[3])
            self.high = float(vars[4])
            self.low = float(vars[5])
            if (float(self.realtime_data['current_price']) == 0.0):
                self.realtime_data['high'] = self.high = float(float(self.realtime_data['pre_close'])*1.1)
                self.realtime_data['low'] = self.low = float(float(self.realtime_data['pre_close'])*0.9)
            
            self.realtime_data['list']['buy_v'] = []
            self.realtime_data['list']['buy_p'] = []
            self.realtime_data['list']['sell_v'] = []
            self.realtime_data['list']['sell_p'] = []
            self.realtime_data['list']['buy_v'].append(int(int(vars[10])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['buy_p'].append('%4.3f'%float(vars[11]))
            else:
                self.realtime_data['list']['buy_p'].append('%4.2f'%float(vars[11]))
            self.realtime_data['list']['buy_v'].append(int(int(vars[12])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['buy_p'].append('%4.3f'%float(vars[13]))
            else:
                self.realtime_data['list']['buy_p'].append('%4.2f'%float(vars[13]))
            self.realtime_data['list']['buy_v'].append(int(int(vars[14])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['buy_p'].append('%4.3f'%float(vars[15]))
            else:
                self.realtime_data['list']['buy_p'].append('%4.2f'%float(vars[15]))
            self.realtime_data['list']['buy_v'].append(int(int(vars[16])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['buy_p'].append('%4.3f'%float(vars[17]))
            else:
                self.realtime_data['list']['buy_p'].append('%4.2f'%float(vars[17]))
            self.realtime_data['list']['buy_v'].append(int(int(vars[18])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['buy_p'].append('%4.3f'%float(vars[19]))
            else:
                self.realtime_data['list']['buy_p'].append('%4.2f'%float(vars[19]))
            self.realtime_data['list']['sell_v'].append(int(int(vars[20])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['sell_p'].append('%4.3f'%float(vars[21]))
            else:
                self.realtime_data['list']['sell_p'].append('%4.2f'%float(vars[21]))
            self.realtime_data['list']['sell_v'].append(int(int(vars[22])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['sell_p'].append('%4.3f'%float(vars[23]))
            else:
                self.realtime_data['list']['sell_p'].append('%4.2f'%float(vars[23]))
            self.realtime_data['list']['sell_v'].append(int(int(vars[24])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['sell_p'].append('%4.3f'%float(vars[25]))
            else:
                self.realtime_data['list']['sell_p'].append('%4.2f'%float(vars[25]))
            self.realtime_data['list']['sell_v'].append(int(int(vars[26])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['sell_p'].append('%4.3f'%float(vars[27]))
            else:
                self.realtime_data['list']['sell_p'].append('%4.2f'%float(vars[27]))
            self.realtime_data['list']['sell_v'].append(int(int(vars[28])/100))
            if (QtCore.QString(u'%s'%(self.sid))[0] == '1' or
                QtCore.QString(u'%s'%(self.sid))[0] == '5'):
                self.realtime_data['list']['sell_p'].append('%4.3f'%float(vars[29]))
            else:
                self.realtime_data['list']['sell_p'].append('%4.2f'%float(vars[29]))

#        if self.realtime_data['current_price'] == 0:
#           self.realtime_data['current_price'] = self.realtime_data['list']['close'][self.datacount-1]

    def mouseMoveEvent(self, event):
        self.m_x = int(event.x())
        self.m_y = int(event.y())
#        self.current_moving_item = -1
        self.repaint()

    def paintEvent(self, event):
        real_painter(self)

#mallticks = {}
#mallticks['time']   = []
#mallticks['price']  = []
#mallticks['volume'] = []
#mallticks['amount'] = []
#mallticks['mma'] = []
def get_all_ticks_today(stockid):
    try:
        if not os.path.exists(stockid):
            return
        mallticks = {}
        mallticks['time']   = []
        mallticks['price']  = []
        mallticks['volume'] = []
        mallticks['amount'] = []
        mallticks['mma'] = []
        mallticks['type_sell'] = []
        df = ts.get_today_ticks(stockid)
        tf = stockid
        tf += "/allticks"
        m1min = stockid 
        m1min += "/1_min"
        if df is not None:
            ft = file(tf, "w")
            f1min = file(m1min, "w")
            dflen = df.shape[0]
            ptime = ""
            mvolume = 0
            mamount = 0
            mtype_sell = 0
            times = df['time'].tolist() 
            prices = df['price'].tolist() 
            volumes = df['volume'].tolist() 
            amounts = df['amount'].tolist() 
            types = df['type'].tolist()
            for i in range(dflen-1,-1,-1):
                if (int(volumes[i]) == 0 or  int(amounts[i]) == 0 or float(prices[i]) == 0):
                    continue
                hh = int(times[i][0:2])
                mm = int(times[i][3:5])
                ctime = times[i][0:5]
                if ((hh == 9) and (mm < 30)):
                    ctime = "09:30"
                if ((hh == 15) and (mm > 0)):
                    ctime = "15:00"
                data = ""
                data += times[i][0:5]
                data += ","
                data += str(times[i])
                data += ","
                data += str(prices[i])
                data += ","
                data += str(volumes[i])
                if (ctime == ptime):
                    mvolume += int(volumes[i])
                    mamount += int(amounts[i])
                    if (str(types[i]) == '卖盘'): 
                        mtype_sell += int(volumes[i])
                    try:
                        index = mallticks['time'].index(ctime)
                        # if index already exist, then update it
                        mallticks['price'][index] = float(prices[i])
                        mallticks['volume'][index] = int(mvolume)
                        mallticks['type_sell'][index] = int(mtype_sell)
                        mallticks['amount'][index] = int(mamount)
                        mallticks['mma'][index] = sum(mallticks['amount'])/(100.0*sum(mallticks['volume']))
                    except:
                        pass
                else:
                    mvolume = int(volumes[i])
                    mamount = int(amounts[i])
                    if (str(types[i]) == '卖盘'): 
                        mtype_sell = int(volumes[i])
                    pr = float(prices[i])
                    mallticks['time'].insert(0,ctime)
                    mallticks['price'].insert(0,pr)
                    mallticks['volume'].insert(0,mvolume)
                    mallticks['type_sell'].insert(0,mtype_sell)
                    mallticks['amount'].insert(0,mamount)
                    mallticks['mma'].insert(0,sum(mallticks['amount'])/(100.0*sum(mallticks['volume'])))
                    ptime = ctime 
                data += ","
                data += str(mvolume)
                data += ","
                data += str(mamount)
                data += ","
                data += str(types[i])
                ft.writelines(data)
                ft.writelines("\n")
            ft.flush()
            ft.close()
            
            dflen = len(mallticks['time'])
            for i in range(dflen-1,-1,-1):
           # for i in range(0,dflen):
                ll = ""
                ll += str(mallticks['time'][i])
                ll += ","
                ll += str(mallticks['price'][i])
                ll += ","
                ll += str(mallticks['volume'][i])
                ll += ","
                ll += str(mallticks['amount'][i])
                ll += ","
                ll += str(mallticks['mma'][i])
                ll += ","
                if (int(mallticks['volume'][i])/int(mallticks['type_sell'][i]) >= 2.0):
                    ll += "1" 
                else:
                    ll += "0" 
                ll += ","
                ll += str(mallticks['type_sell'][i])
                f1min.writelines(ll)
                f1min.writelines("\n")
            f1min.flush()
            f1min.close()
            print "OK to get_today_ticks!"
            return df
        else:
            print "failed to get_today_ticks !"
            return None
    except Exception as e:
        print "failed to get_today_ticks from website"
        print(e)
        return None

stockid = ""
if __name__ == '__main__':
    app = QtGui.QApplication(sys.argv)
    stockid = str(sys.argv[1])[0:6]
    if not os.path.exists(stockid):
        print stockid, " does not exist."
        exit(0)

    if os.path.exists(stockid+"/1_min"):
        rt = RealtimeQuotesWidget(stockid)
        rt.show()
        app.exec_()
    else:#if (get_all_ticks_today(stockid) is not None):
        rt = RealtimeQuotesWidget(sys.argv[1])
        rt.show()
        app.exec_()
#    else:
#        exit(0)
