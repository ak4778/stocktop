#!/usr/bin/python
#encoding=utf-8
#-*- coding: utf-8 -*-
#
# preferred screen height ratio      Kline:Volume:Indicator(MACD,RSI,KDJ) = 59:16:25
#

import this
import os
import sys
import time
import talib
import chart
import locale
import random
import datetime
import indicators
import numpy as np
import pandas as pd
import tushare as ts
#import realtime_quotes as rq
import minutes as rq
import matplotlib.pyplot as plt
from tushare.util import dateu as du
from PyQt4 import QtGui,QtCore,Qt
from PyQt4.QtGui import *
from PyQt4.Qt import *

#KKEYS = ['5', '15', '30', '60', 'D', 'W', 'M']
#K_FILE_NAME = {'5' : '5_minutes',  	\
#               '15': '15_minutes', 	\
#               '30': '30_minutes', 	\
#	        '60': '60_minutes', 	\
#	        'D' : 'tradehistory', 	\
#               'W' : 'akweekly', 	\
#               'M' : 'akmonthly'	\
#	      }
#KLABEL = {'5': '5分','15': '15分','30': '30分','60': '60分','D': '日K','W': '周K','M':'月K'}

KKEYS = ['5', '15', '30', '60', 'E', 'W', 'M']
K_FILE_NAME = {'5' : '5_minutes',  	\
               '15': '15_minutes', 	\
               '30': '30_minutes', 	\
	       '60': '60_minutes', 	\
	       'E' : 'akdaily', 	\
               'W' : 'akweekly', 	\
               'M' : 'akmonthly'	\
	      }
KLABEL = {'5': '5分','15': '15分','30': '30分','60': '60分','E': '日K','W': '周K','M':'月K'}
DEFAULT_DAYS = 108
stockid = ""
stockname = ""
real_open = 0.0
real_pre_close = 0.0
real_high = 0.0
real_low = 0.0
ticks_today = {}
ticks_today['time']   = ['','','','','','','','','','']
ticks_today['price']  = ['','','','','','','','','','']
ticks_today['volume'] = ['','','','','','','','','','']
ticks_today['type']   = ['','','','','','','','','','']

def isAllNan(tlist):
    aa=np.array(tlist)
    return (len(tlist) == len(aa[np.isnan(aa)]))

#============================================================================#
# Implementation of a MultiPageMainWidget using a QStackedWidget #
#----------------------------------------------------------------------------#
class MultiPageMainWidget(QtGui.QWidget):

    currentIndexChanged = QtCore.pyqtSignal(int)

    pageTitleChanged = QtCore.pyqtSignal(str)

    def __init__(self, parent=None):
        super(MultiPageMainWidget, self).__init__(parent)
        self.swidth = QApplication.desktop().availableGeometry().width()
        self.sheight = QApplication.desktop().availableGeometry().height()
        #print "www = ", self.swidth, " hhh = ", self.sheight
        self.setMinimumSize(998, 482) #设置窗口最小尺寸
        self.setGeometry(0, 0, self.swidth, self.sheight)
        self.setWindowTitle(QtCore.QString(u'Stouck hunter'))
        self.setStyleSheet("QWidget { background-color: black}")
        self.setWindowIcon(QtGui.QIcon('ruby.png'))
        self.setMouseTracking(True)
        self.setCursor(Qt.CrossCursor)

        self.toggleFullScreenAction = QtGui.QAction("", self, shortcut="F11")
#                triggered(bool)=self.setFullScreen)
        self.toggleFullScreenAction.setCheckable(True)
        self.addAction(self.toggleFullScreenAction)
        self.toggleFullScreenAction.triggered.connect(self.setFullScreen)
#        self.connect(toggleFullScreenAction, QtCore.SIGNAL('triggered(bool)'),
#                     self, QtCore.SLOT("setFullScreen(bool)"))
#        quitAction = QtGui.QAction("E&xit", self, shortcut="Ctrl+Q",
#                triggered=QtGui.qApp.quit)
#        self.addAction(quitAction)

        # MAGIC
        # It is important that the combo box has an object name beginning
        # with '__qt__passive_', otherwise, it is inactive in the form editor
        # of the designer and you can't change the current page via the
        # combo box.
        # MAGIC
        self.setMouseTracking(True)
        self.setStyleSheet("QWidget { background-color: black}")
        self.stackWidget = QtGui.QStackedWidget()
        self.kwidget = KHistoryWidget(stockid)
        #self.rwidget = rq.RealTime_Quotes()
        self.rwidget = rq.RealtimeQuotesWidget(stockid)
        self.chartwidget = chart.ChartWidget(stockid)
        self.fullscreen = 0
        self.lastindex = 0

        self.kwidget.setParent(self.stackWidget)
        self.rwidget.setParent(self.stackWidget)
        self.chartwidget.setParent(self.stackWidget)

#        self.kwidget.installEventFilter(self)
#        self.rwidget.installEventFilter(self)

        self.stackWidget.insertWidget(0, self.rwidget)
        self.stackWidget.insertWidget(1, self.kwidget)
        self.stackWidget.insertWidget(2, self.chartwidget)
        self.stackWidget.setMouseTracking(True)

        self.layout = QtGui.QVBoxLayout()
        self.layout.addWidget(self.stackWidget)
        self.layout.setContentsMargins(1, 1, 1, 1);
        self.setLayout(self.layout)
        self.layout.setSpacing(0)
        self.stackWidget.setCurrentIndex(0)

    def closeEvent(self, e):
        self.kwidget.terminal()
        self.rwidget.terminal()
        e.accept()
        sys.exit()

#    @QtCore.pyqtSlot()
    def setFullScreen(self, on):
        if ((self.windowState() & Qt.WindowFullScreen) == on):
            return;
        if (on):
            self.setWindowState(self.windowState() | Qt.WindowFullScreen)
        else:
            self.setWindowState(self.windowState() & ~Qt.WindowFullScreen)

    #def eventFilter(self, object, event):
    #    print event.type()
    #    if isinstance(object, QtGui.QWidget):
    #        #mouseMoveEvent
    #        if (event.type() == 5):
    #            print "muli eventFilter"
    #            return True
    #    if (event.type() == 68):
    #        #self.kwidget.keyPressEvent(event)
    #        return False
    #    return QtGui.QWidget.eventFilter(self, object, event)

    def keyPressEvent(self, event):
        key = event.key()
        index = self.stackWidget.currentIndex()
        if key == QtCore.Qt.Key_F11:
            super(MultiPageMainWidget, self).mouseMoveEvent(event)
        #elif key == QtCore.Qt.Key_F12:
        elif key == QtCore.Qt.Key_Return:
            if (index == 0):
                self.stackWidget.setCurrentIndex(1)
            elif (index == 1):
                self.stackWidget.setCurrentIndex(0)
            elif (index == 2):
                self.stackWidget.setCurrentIndex(self.lastindex)
        elif key == QtCore.Qt.Key_F10:
            if os.path.exists(stockid + "/ddr"):
                if (index != 2):
                    self.lastindex = index
                self.stackWidget.setCurrentIndex(2)
#        elif key == QtCore.Qt.Key_Q:
#            time.sleep(2)
#            QtGui.qApp.quit()
        else:
            self.widget(self.getCurrentIndex()).keyPressEvent(event)

    def mouseMoveEvent(self, event):
        self.widget(self.getCurrentIndex()).mouseMoveEvent(event)
#            super(MultiPageMainWidget, self).mouseMoveEvent(event)

    def count(self):
        return self.stackWidget.count()

    def widget(self, index):
        return self.stackWidget.widget(index)

    @QtCore.pyqtSlot(QtGui.QWidget)
    def addPage(self, page):
        self.insertPage(self.count(), page)

    @QtCore.pyqtSlot(int, QtGui.QWidget)
    def insertPage(self, index, page):
        page.setParent(self.stackWidget)
        self.stackWidget.insertWidget(index, page)
        title = page.windowTitle()
        if title.isEmpty():
            page.setWindowTitle(title)

    @QtCore.pyqtSlot(int)
    def removePage(self, index):
        widget = self.stackWidget.widget(index)
        self.stackWidget.removeWidget(widget)

    def getPageTitle(self):
        return self.stackWidget.currentWidget().windowTitle()
    
    @QtCore.pyqtSlot(str)
    def setPageTitle(self, newTitle):
        self.stackWidget.currentWidget().setWindowTitle(newTitle)
        self.pageTitleChanged.emit(newTitle)

    def getCurrentIndex(self):
        return self.stackWidget.currentIndex()

    @QtCore.pyqtSlot(int)
    def setCurrentIndex(self, index):
        if index != self.getCurrentIndex():
            self.stackWidget.setCurrentIndex(index)
            self.currentIndexChanged.emit(index)

    pageTitle = QtCore.pyqtProperty(str, fget=getPageTitle, fset=setPageTitle, stored=False)
    currentIndex = QtCore.pyqtProperty(int, fget=getCurrentIndex, fset=setCurrentIndex)


#============================================================================#
# Main for testing the class                                                 #
#----------------------------------------------------------------------------#
class TitleWidget(QtGui.QWidget):
    def __init__(self, code, name, parent=None):
        super(TitleWidget, self).__init__(parent)

        self.code = str(code)
        self.name = str(name)

        self.floatBased = False
        self.antialiased = False
        self.frameNo = 0

        self.setBackgroundRole(QtGui.QPalette.Base)
        self.setSizePolicy(QtGui.QSizePolicy.Expanding,
                QtGui.QSizePolicy.Expanding)
    
    def setTitleData(self, code, name):
        self.code = str(code)
        self.name = str(name)
        #self.update()

    def minimumSizeHint(self):
        return QtCore.QSize(198, 57)

    def sizeHint(self):
        return QtCore.QSize(198, 57)

    def paintEvent(self, event):
        painter = QtGui.QPainter(self)
        painter.setRenderHint(QtGui.QPainter.Antialiasing, self.antialiased)
        painter.setPen(QtGui.QPen( QtCore.Qt.yellow, 1, QtCore.Qt.SolidLine))
        #painter.drawRect(0,0,self.width()-1,self.height()-1)
        radius = min(self.width()/2.0,self.height()/2.0)
        painter.setFont(QtGui.QFont('Serif',12,QtGui.QFont.Bold))
        #self.setPen('blue')
        painter.setPen(QtGui.QPen( QtCore.Qt.blue, 1, QtCore.Qt.SolidLine))
        fh = painter.fontMetrics().height()   #获得文字高度
        fw = painter.fontMetrics().width(self.code)
        stockID_str = self.code
        starty = 18
        painter.drawText(18, starty + (self.height() - fh) / 2.0, stockID_str)
        painter.setPen(QtGui.QPen( QtCore.Qt.yellow, 1, QtCore.Qt.SolidLine))
        painter.drawText(self.width() - 18 - painter.fontMetrics().width(self.name), starty + (self.height() - fh) / 2.0, self.name)

class FlashWidget(QtGui.QWidget):
    def __init__(self, price, percent, up, parent=None):
        super(FlashWidget, self).__init__(parent)

        self.price = str(price)
        self.percent = str(percent)
        self.up = int(up)
        self.setMouseTracking(True)

        self.floatBased = False
        self.antialiased = False
        self.frameNo = 0

        self.setBackgroundRole(QtGui.QPalette.Base)
        self.setSizePolicy(QtGui.QSizePolicy.Expanding,
                QtGui.QSizePolicy.Expanding)
        ftimer = QtCore.QTimer(self)
        ftimer.timeout.connect(self.FwNextAnimationFrame)
        ftimer.start(1158)
    
    def setFwData(self, price, percent, flag):
        self.price = str(price)
        self.percent = str(percent)
        self.up = int(flag)
        # it will cause deadloop 
        # update() will call parent widget paintEvent
        # update cannot be called within parent's paintEvent
        #self.update()

    def mouseMoveEvent(self, event):
        pass

    def setFloatBased(self, floatBased):
        self.floatBased = floatBased
        self.update()

    def setAntialiased(self, antialiased):
        self.antialiased = antialiased
        self.update()

    def minimumSizeHint(self):
        return QtCore.QSize(198, 57)

    def sizeHint(self):
        return QtCore.QSize(198, 57)

    def FwNextAnimationFrame(self):
        self.frameNo += 1
        self.update()

    def paintEvent(self, event):
        painter = QtGui.QPainter(self)
        painter.setRenderHint(QtGui.QPainter.Antialiasing, self.antialiased)
        #painter.translate(self.width() / 2.0, self.height() / 2.0)
        painter.setPen(QtGui.QPen( QtCore.Qt.yellow, 1, QtCore.Qt.SolidLine))
        #painter.drawRect(0,0,self.width()-1,self.height()-1)
        radius = min(self.width()/2.0,self.height()/2.0)
        #painter.drawEllipse(QtCore.QPoint((self.width()/2.0), (self.height()/2.0)), radius, radius)

        painter.setFont(QtGui.QFont('Serif',20,QtGui.QFont.Bold))
        painter.setPen(QtGui.QPen( QtCore.Qt.red, 1, QtCore.Qt.SolidLine))

        fw = painter.fontMetrics().width(self.price)
        fh = painter.fontMetrics().height() 

        cprice = self.price
        starty = 14
        for diameter in range(0, 256, 9):
            delta = abs((self.frameNo % 128) - diameter / 2)
            alpha = 255 - (delta * delta) / 4 - diameter
            if alpha > 0:
                if (self.up > 0):
                    #painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, alpha), 3))
                    painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, 255), 3))
                else:
                    #painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, alpha), 3))
                    painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, 255), 3))

                if self.floatBased:
                    painter.drawText((self.width() - fw) / 2, starty + (self.height() - fh) / 2.0, cprice)
                else:
                    painter.drawText((self.width() - fw) / 2, starty + (self.height() - fh) / 2.0, cprice)

        painter.setFont(QtGui.QFont('Serif',12,QtGui.QFont.Bold))

        ss = self.percent
        fw=painter.fontMetrics().width(ss)
        starty += 20
        for diameter in range(0, 256, 9):
            delta = abs((self.frameNo % 128) - diameter / 2)
            alpha = 255 - (delta * delta) / 4 - diameter
            if alpha > 0:
                if (self.up > 0):
                    #painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, alpha), 3))
                    painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, 255), 3))
                else:
                    #painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, alpha), 3))
                    painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, 255), 3))

                if self.floatBased:
                    painter.drawText((self.width() - fw) / 2, starty + (self.height() - fh) / 2.0, ss)
                else:
                    painter.drawText((self.width() - fw) / 2, starty + (self.height() - fh) / 2.0, ss)

 
class report_painter(QtCore.QObject):
    '''绘制行情类'''
    def __init__(self,parent):
        QtCore.QObject.__init__(self, parent)
         
        #初始化
        self.parent = parent
        self.painter = QtGui.QPainter()
        self.painter.begin(self.parent)
 
        #设置抗锯齿
        self.painter.setRenderHint(QtGui.QPainter.Antialiasing)
        #度量尺对象
        self.metrics = self.painter.fontMetrics()
         
        #self.seperatorHeight = self.metrics.height()/2 + 4
        self.seperatorHeight = 18
        self.circleradius = 9
        self.titley = 20
        #print "seperatorHeight = ", self.seperatorHeight

        #设置字体库
        self.fonts = dict()
        self.fonts['default'] = QtGui.QFont('Serif', 9, QtGui.QFont.Normal)
        #self.fonts['default'] = QtGui.QFont('Serif', 9, QtGui.QFont.Light)
        #self.fonts['default'] = QtGui.QFont('Helvetica', 9, QtGui.QFont.Light)
        #self.fonts['default'] = QtGui.QFont('Monospace', 9, QtGui.QFont.Light)
        self.fonts['yahei_14_bold']= QtGui.QFont('Serif',12,QtGui.QFont.Bold)
        self.fonts['yahei_18_bold']= QtGui.QFont('Serif',20,QtGui.QFont.Bold)
        #self.fonts['yahei_14']= QtGui.QFont('Serif',12,QtGui.QFont.Light)
        self.fonts['yahei_14_normal']= QtGui.QFont('Serif',12,QtGui.QFont.Normal)
        self.setFont('default')
 
        #设置笔刷样式库
        self.pens = dict()
         
        #红色 1px粗  1px点 2px距 线条
        self.pens['gray_1px_dashline'] =  QtGui.QPen( QtCore.Qt.gray, 1, QtCore.Qt.DashLine)
        #self.pens['gray_1px_dashline'].setDashPattern([1,2])
        self.pens['gray_1px_dashline'].setDashPattern([1,5])
 
        #红色 1px粗 实线条
        self.pens['red'] = QtGui.QPen( QtCore.Qt.red, 1, QtCore.Qt.SolidLine)
        self.pens['mred'] = QtGui.QPen( QColor(255,64,64), 1, QtCore.Qt.SolidLine)
        #红色 3px粗 实线条
        self.pens['red_2px'] = QtGui.QPen( QtCore.Qt.red, 2, QtCore.Qt.SolidLine)
        #红色 2px粗 实线条
        self.pens['red_3px'] = QtGui.QPen( QtCore.Qt.red, 3, QtCore.Qt.SolidLine)
        #黄色 1px粗 实线条
        self.pens['yellow'] = QtGui.QPen( QtCore.Qt.yellow, 1, QtCore.Qt.SolidLine)
        self.pens['myellow'] = QtGui.QPen( QColor(207,207,64), 1, QtCore.Qt.SolidLine)
        #白色 1px粗 实线条
        self.pens['white']  = QtGui.QPen( QtCore.Qt.white , 1, QtCore.Qt.SolidLine)
        self.pens['black']  = QtGui.QPen( QtCore.Qt.black , 1, QtCore.Qt.SolidLine)
        #self.pens['blue']  = QtGui.QPen( QtCore.Qt.blue , 1, QtCore.Qt.SolidLine)
        self.pens['blue']  = QtGui.QPen( QColor(64,64,255) , 1, QtCore.Qt.SolidLine)
        self.pens['magan']  = QtGui.QPen( QColor(255,0,255) , 1, QtCore.Qt.SolidLine)
        self.pens['maganc']  = QtGui.QPen( QColor(85,88,255) , 1, QtCore.Qt.SolidLine)
        #灰色 1px粗 实线条
        self.pens['gray']   = QtGui.QPen( QtCore.Qt.gray, 1, QtCore.Qt.SolidLine)
        #绿色 1px粗 实线条
        #self.pens['green']   = QtGui.QPen( QtCore.Qt.green, 1, QtCore.Qt.SolidLine)
        #self.pens['green']   = QtGui.QPen( QtCore.Qt.darkGreen, 1, QtCore.Qt.SolidLine)
        self.pens['green']   = QtGui.QPen( QColor(64,160,64), 1, QtCore.Qt.SolidLine)
        #绿色 3px粗 实线条
        self.pens['green_2px']   = QtGui.QPen( QtCore.Qt.green, 2, QtCore.Qt.SolidLine)
        #亮蓝 1px粗  1px点 2px距 线条
        self.pens['cyan_1px_dashline'] =  QtGui.QPen( QtCore.Qt.cyan, 1, QtCore.Qt.DashLine)
        #self.pens['cyan_1px_dashline'].setDashPattern([3,2])
        #space = 4;
        #dashes << 1 << space << 3 << space << 9 << space
        #   << 27 << space << 9 << space;
        self.pens['cyan_1px_dashline'].setDashPattern([1,9])
        #获得窗口的长和宽
        size      = self.parent.size()
        self.w    = size.width()
        self.h    = size.height()
        #print "set width self.w=",self.w, "self.h=",self.h

        self.hscale = 100
 
        #设置grid的上下左右补丁边距
        self.left_text_padding   = 6
        self.grid_padding_left   = 58  #左侧补丁边距
        self.grid_padding_right  = 218 #右侧补丁边距
        self.grid_padding_top    = 25  #顶部补丁边距
        self.grid_padding_bottom = 17  #底部补丁边距
        #self.ratio               = 0.7 #price/volume
        self.pr = 59.0
        self.vr = 16.0
        self.ir = 25.0
        #self.ratio               = self.pr/100
        self.pratio = 0.59
        self.vratio = 0.16
        self.iratio = 0.25
    
        self.widthOf_KLine = self.parent.maxWidthOfKline
        # calculate the appropriate widthOf_KLine
        while ((self.parent.last-self.parent.first)*(self.widthOf_KLine+3) > (self.w-self.grid_padding_left-self.grid_padding_right-2) and (self.widthOf_KLine > 1)) and ((self.widthOf_KLine - 2) > 0):
            self.widthOf_KLine -= 2
        #print "total days = ", self.parent.datacount,"displayed days = ", self.parent.last-self.parent.first, "widthOf_KLine = ", self.widthOf_KLine
        #print "price_view_high = ", self.parent.price_view_high,"price_view_low = ", self.parent.price_view_low
        sd = self.parent.last - self.parent.first
        if (sd > 0 and self.widthOf_KLine > 0):
            self.w_radio = (self.w-self.grid_padding_left-self.grid_padding_right-2)/(self.widthOf_KLine*float(self.parent.last-self.parent.first))
            print "rrrr=",self.w_radio
            print "wwww=",self.widthOf_KLine
            if self.w_radio > 12:
                self.w_radio = 2

        #开始绘制
        self.start_paint()
        self.painter.end()   #结束

    '''绘制流程步骤'''
    def start_paint(self):
        self.FrameGridPaint()
        self.timelinePaint(self.parent.first, self.parent.last)
        self.topInfoPaint(self.parent.current_moving_item)
        self.rulerPaint()
        self.VolumeGridPaint()
        self.KlinePaint(self.parent.first, self.parent.last)
        self.chanlunPaint(self.parent.first, self.parent.last)
        self.volumePaint(self.parent.first, self.parent.last)
        self.volMovingUpdate(self.parent.current_moving_item)

        if self.parent.dzxflag == 1:
            self.DZXGridPaint()
            self.DZX_Paint(self.parent.first, self.parent.last)
            self.DZXMovingUpdate(self.parent.current_moving_item)

        if self.parent.kdjflag == 1:
            self.KDJGridPaint()
            self.KDJ_Paint(self.parent.first, self.parent.last)
            self.KDJMovingUpdate(self.parent.current_moving_item)

        if self.parent.rsiflag == 1:
            self.RSIGridPaint()
            self.RSI_Paint(self.parent.first, self.parent.last)
            self.RSIMovingUpdate(self.parent.current_moving_item)

        if self.parent.macdflag == 1:
            self.MACDGridPaint()
            self.macdMovingUpdate(self.parent.current_moving_item)
            self.DIFF_DEA_MACD_Paint(self.parent.first, self.parent.last)

        if (self.parent.sma == 1):
            self.maTrendPaint(self.parent.first, self.parent.last)

        if (self.parent.boll == 1):
            self.bollPaint(self.parent.first, self.parent.last)

        if (self.parent.bbi == 1):
            self.bbiPaint(self.parent.first, self.parent.last)

        self.xyPaint(self.parent.first, self.parent.last)
        self.datetime_Paint()
        self.rightGridPaint()
        self.ktypePaint()

    '''设置使用的字体'''
    def setFont(self,code='default'):
        self.painter.setFont(self.fonts[code])
         
    '''设置使用的笔刷'''
    def setPen(self,code='default'):
        self.painter.setPen(self.pens[code])
         
    '''绘制股价走势表格'''
    def FrameGridPaint(self):
        self.setPen('gray')
        self.painter.setBrush(QtCore.Qt.NoBrush)
         
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top + self.grid_padding_bottom
 
        grid_height = self.h - sum_height
 
        #画边框
        self.painter.drawRect(self.grid_padding_left, self.grid_padding_top, self.w-sum_width, self.h-sum_height)
        self.painter.drawRect(0,0,self.w,self.h)
        self.setPen('gray')
        self.painter.setBrush(QtCore.Qt.darkGray)
        self.painter.setBrush(QtCore.Qt.darkBlue)
        self.painter.drawRoundRect(1,1,self.w-1,2,1,1)
        self.setPen('gray')
        self.painter.setBrush(QtCore.Qt.NoBrush)
        self.painter.drawLine(self.grid_padding_left, 0, self.grid_padding_left, self.h)

#        print "metrics.height() = ", self.metrics.height()
        #成交量和走势的分界线
        self.painter.drawLine(self.grid_padding_left, grid_height*self.pratio + self.grid_padding_top,
                            self.w-self.grid_padding_right, grid_height*self.pratio + self.grid_padding_top)
        #draw seperator line between vol and macd
        self.painter.drawLine(self.grid_padding_left, grid_height*(self.pratio+self.vratio) + self.grid_padding_top,
                            self.w-self.grid_padding_right, grid_height*(self.pratio+self.vratio) + self.grid_padding_top)
 
        #其他线条
        self.painter.drawLine(0, self.h - self.grid_padding_bottom, self.w-self.grid_padding_right,self.h-self.grid_padding_bottom)
        self.painter.drawLine(0,self.h-self.grid_padding_bottom+16,self.w,self.h-self.grid_padding_bottom+16)
 
        self.painter.drawLine(self.w-self.grid_padding_right,0,
                            self.w-self.grid_padding_right,self.h-self.grid_padding_bottom+16)
        self.painter.drawLine(self.w-self.grid_padding_right,self.h-self.grid_padding_bottom,
                            self.w,self.h-self.grid_padding_bottom-1)
#        self.painter.drawLine(self.w-self.grid_padding_right+44,0,
#                            self.w-self.grid_padding_right+44,self.h-self.grid_padding_bottom+16)
        self.setPen('blue')
        #右下角文字
        self.painter.drawText(self.w-self.grid_padding_right+self.left_text_padding,self.h-self.grid_padding_bottom+12,QtCore.QString(u'K Line'))

    def DZXMovingUpdate(self, index):
        if (self.parent.datacount == 0):return
       # print "macdMovingUpdate()"
        if ((self.parent.first + index) > self.parent.last):
            index = self.parent.datacount - 1 - self.parent.first

        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        str1 = "DZX"
        x = self.grid_padding_left + self.left_text_padding
        y = grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6
        self.setFont('default')

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'AK %.2f'%(self.parent.stk_data['list']['ak'][self.parent.first+index])
        self.setPen('yellow')
        self.painter.drawText(x, y, str1) 

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'AD1 %.2f'%(self.parent.stk_data['list']['ad1'][self.parent.first+index])
        self.setPen('white')
        self.painter.drawText(x, y, str1)

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'AJ %.2f'%(float(self.parent.stk_data['list']['aj'][self.parent.first+index]))
        self.setPen('magan')
        self.painter.drawText(x, y, str1)

        x = self.w-self.grid_padding_right
        str1 = 'Date'
        str1 += ' '
        str1 += '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        str1 += '  '
        x -= self.painter.fontMetrics().width(str1)
        str1 = 'Date'
        self.setPen('blue')
        self.painter.drawText(x, y, str1) 
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        self.setPen('magan')
        self.painter.drawText(x, y, str1)

    def KDJMovingUpdate(self, index):
        if (self.parent.datacount == 0):return
       # print "macdMovingUpdate()"
        if ((self.parent.first + index) > self.parent.last):
            index = self.parent.datacount - 1 - self.parent.first

        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        str1 = "KDJ(9,3,3)"
        x = self.grid_padding_left + self.left_text_padding
        y = grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6
        self.setFont('default')

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'K %.2f'%(self.parent.stk_data['list']['kdjk'][self.parent.first+index])
        self.setPen('yellow')
        self.painter.drawText(x, y, str1) 

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'D %.2f'%(self.parent.stk_data['list']['kdjd'][self.parent.first+index])
        self.setPen('white')
        self.painter.drawText(x, y, str1)

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'J %.2f'%(float(self.parent.stk_data['list']['kdjj'][self.parent.first+index]))
        self.setPen('magan')
        self.painter.drawText(x, y, str1)

        x = self.w-self.grid_padding_right
        str1 = 'Date'
        str1 += ' '
        str1 += '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        str1 += '  '
        x -= self.painter.fontMetrics().width(str1)
        str1 = 'Date'
        self.setPen('blue')
        self.painter.drawText(x, y, str1) 
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        self.setPen('magan')
        self.painter.drawText(x, y, str1)

    def DZXGridPaint(self):
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height

        self.setPen('black')
        self.painter.setBrush(QtGui.QColor(24, 85, 210))
        self.painter.drawRoundRect(self.grid_padding_left+self.left_text_padding-2, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+1,self.painter.fontMetrics().width("DZX")+4,self.painter.fontMetrics().height()+1)

        self.setPen('white')
        self.painter.setBrush(QtCore.Qt.NoBrush)
        self.painter.drawText(self.grid_padding_left+self.left_text_padding, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6,"DZX") 

        #if (self.parent.diff_dea_range == 0 or self.parent.macd_range == 0): return
        #if (self.parent.datacount == 0):return

        self.setPen('magan')

        high = float(self.parent.DZX_max)
        low  = float(self.parent.DZX_min)
 
        limit_top = high 
        limit_low = low
         
        px_h_radio = float((high-low)/(grid_height*self.iratio-self.seperatorHeight))

        # draw macd range square
        self.painter.drawRect(2,self.seperatorHeight+self.grid_padding_top+(grid_height*(self.pratio+self.vratio)),self.grid_padding_left-4, grid_height*self.iratio-1-self.seperatorHeight)
        self.painter.drawRect(2,self.grid_padding_top,self.grid_padding_left-4, grid_height*self.pratio)
         
        self.setPen('gray_1px_dashline')
         
        grid_num = 2
        x = grid_num
        cnt = (grid_height*(self.iratio)-self.seperatorHeight)/grid_num
        for i in range(0,grid_num):
            self.setPen('gray_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+(grid_height*(self.pratio+self.vratio))+i*cnt+self.seperatorHeight
            x1 = self.grid_padding_left
            x2 = self.w - self.grid_padding_right
             
            # draw volume dot line
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            vol_int = '%.2f'%float(cnt*x*px_h_radio+self.parent.DZX_min)
            vol_str = str(vol_int)
            fw = self.painter.fontMetrics().width(vol_str) #获得文字宽度
            if i == 0:
                fh = self.painter.fontMetrics().height()   #获得文字高度
            else:
                fh = self.painter.fontMetrics().height()/2   #获得文字高度
            self.setPen('red')
            #self.painter.drawText(x2+2,y1+fh,vol_str) #写入文字
            self.painter.drawText(self.left_text_padding,y1+fh,vol_str)
        #    self.setPen('white')
        #    self.painter.drawText(x1-2-self.metrics.width(str(x)),y1+fh,str(x))    #写入文字
            x-=1

        self.setPen('red')
        fh = self.painter.fontMetrics().height()/2   #获得文字高度
        self.painter.drawText(self.left_text_padding, self.h-self.grid_padding_bottom-fh,'%.2f'%(limit_low))


    def KDJGridPaint(self):
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height

        self.setPen('black')
        self.painter.setBrush(QtGui.QColor(24, 85, 210))
        self.painter.drawRoundRect(self.grid_padding_left+self.left_text_padding-2, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+1,self.painter.fontMetrics().width("KDJ(9,3,3)")+4,self.painter.fontMetrics().height()+1)

        self.setPen('white')
        self.painter.setBrush(QtCore.Qt.NoBrush)
        self.painter.drawText(self.grid_padding_left+self.left_text_padding, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6,"KDJ(9,3,3)") 

        #if (self.parent.diff_dea_range == 0 or self.parent.macd_range == 0): return
        #if (self.parent.datacount == 0):return

        self.setPen('magan')

        high = float(self.parent.KDJ_max)
        low  = float(self.parent.KDJ_min)
 
        limit_top = high 
        limit_low = low
         
        px_h_radio = float((high-low)/(grid_height*self.iratio-self.seperatorHeight))

        # draw macd range square
        self.painter.drawRect(2,self.seperatorHeight+self.grid_padding_top+(grid_height*(self.pratio+self.vratio)),self.grid_padding_left-4, grid_height*self.iratio-1-self.seperatorHeight)
        self.painter.drawRect(2,self.grid_padding_top,self.grid_padding_left-4, grid_height*self.pratio)
         
        self.setPen('gray_1px_dashline')
         
        grid_num = 2
        x = grid_num
        cnt = (grid_height*(self.iratio)-self.seperatorHeight)/grid_num
        for i in range(0,grid_num):
            self.setPen('gray_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+(grid_height*(self.pratio+self.vratio))+i*cnt+self.seperatorHeight
            x1 = self.grid_padding_left
            x2 = self.w - self.grid_padding_right
             
            # draw volume dot line
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            vol_int = '%.2f'%float(cnt*x*px_h_radio+self.parent.KDJ_min)
            vol_str = str(vol_int)
            fw = self.painter.fontMetrics().width(vol_str) #获得文字宽度
            if i == 0:
                fh = self.painter.fontMetrics().height()   #获得文字高度
            else:
                fh = self.painter.fontMetrics().height()/2   #获得文字高度
            self.setPen('red')
            #self.painter.drawText(x2+2,y1+fh,vol_str) #写入文字
            self.painter.drawText(self.left_text_padding,y1+fh,vol_str)
        #    self.setPen('white')
        #    self.painter.drawText(x1-2-self.metrics.width(str(x)),y1+fh,str(x))    #写入文字
            x-=1

        self.setPen('red')
        fh = self.painter.fontMetrics().height()/2   #获得文字高度
        self.painter.drawText(self.left_text_padding, self.h-self.grid_padding_bottom-fh,'%.2f'%(limit_low))

    def DZX_Paint(self, first, last):
        if (self.parent.DZX_max == 0 or self.parent.DZX_min == 0): return
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
#        print "DIFF_DEA_MACD_Paint()"
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h-sum_height+1
         
        high = float(self.parent.DZX_max)
        low  = float(self.parent.DZX_min)

        limit_top = high 
        limit_low = low
         
        x1 = self.grid_padding_left
        x2 = self.w-self.grid_padding_right
             
        h_radio = (grid_height*self.iratio-self.seperatorHeight)/((limit_top-limit_low))

        self.painter.setBrush(QtCore.Qt.NoBrush)
        w = self.grid_padding_left+self.widthOf_KLine
        self.setPen('yellow')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            if pd.isnull(self.parent.stk_data['list']['ak'][k]):
                i += 1
                continue 
            diff = float(self.parent.stk_data['list']['ak'][k])
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            if pd.isnull(diff):
                i += 1
                pass
            else:
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1
        
        for k in range(first+i, last):
            diff = float(self.parent.stk_data['list']['ak'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.setPen('yellow')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

#         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            diff = float(self.parent.stk_data['list']['ak'][first+k])
            if pd.isnull(diff):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('yellow')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')

        self.setPen('white')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            if pd.isnull(self.parent.stk_data['list']['ad1'][k]):
                i += 1
                continue 
            dea = float(self.parent.stk_data['list']['ad1'][k])
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            if pd.isnull(dea):
                i += 1
                pass
            else:
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            dea = float(self.parent.stk_data['list']['ad1'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.painter.setBrush(QtCore.Qt.NoBrush)
            self.setPen('white')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

  #         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            dea = float(self.parent.stk_data['list']['ad1'][first+k])

            if pd.isnull(dea):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('white')
                self.painter.setBrush(QtCore.Qt.white)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')

        self.setPen('magan')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            if pd.isnull(self.parent.stk_data['list']['aj'][k]):
                i += 1
                continue 
            dea = float(self.parent.stk_data['list']['aj'][k])
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            if pd.isnull(dea):
                i += 1
                pass
            else:
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            dea = float(self.parent.stk_data['list']['aj'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.painter.setBrush(QtCore.Qt.NoBrush)
            self.setPen('magan')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

  #         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            dea = float(self.parent.stk_data['list']['aj'][first+k])

            if pd.isnull(dea):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('red')
                self.painter.setBrush(QColor(255,0,255))
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)

        # draw Golden Cross
        k = 0 
        for i in range(first+1,last):
            diffp = float(self.parent.stk_data['list']['aj'][i-1])
            deap = float(self.parent.stk_data['list']['ak'][i-1])
            diff = float(self.parent.stk_data['list']['aj'][i])
            dea = float(self.parent.stk_data['list']['ak'][i])
            if pd.isnull(diff) or pd.isnull(dea) or pd.isnull(diffp) or pd.isnull(deap):
                k += 1
                continue
            else:
                pass
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            y1 = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight

            self.setPen('green')
            self.painter.setBrush(QColor(64,160,64))
            
            if (diffp < deap) and ((diff > dea)): 
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1,self.widthOf_KLine,self.parent.image_up.height()),self.parent.image_up)
            if (diffp > deap) and ((diff < dea)): 
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1-self.parent.image_down.height(),self.widthOf_KLine,self.parent.image_down.height()),self.parent.image_down)
            k += 1


    def KDJ_Paint(self, first, last):
        if (self.parent.KDJ_max == 0 or self.parent.KDJ_min == 0): return
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
#        print "DIFF_DEA_MACD_Paint()"
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h-sum_height+1
         
        high = float(self.parent.KDJ_max)
        low  = float(self.parent.KDJ_min)

        limit_top = high 
        limit_low = low
         
        x1 = self.grid_padding_left
        x2 = self.w-self.grid_padding_right
             
        h_radio = (grid_height*self.iratio-self.seperatorHeight)/((limit_top-limit_low))

        self.painter.setBrush(QtCore.Qt.NoBrush)
        w = self.grid_padding_left+self.widthOf_KLine
        self.setPen('yellow')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            diff = float(self.parent.stk_data['list']['kdjk'][k])
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            if pd.isnull(diff):
                i += 1
                pass
            else:
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1
        
        for k in range(first+i, last):
            diff = float(self.parent.stk_data['list']['kdjk'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.setPen('yellow')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

#         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            diff = float(self.parent.stk_data['list']['kdjk'][first+k])
            if pd.isnull(diff):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('yellow')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')

        self.setPen('white')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            dea = float(self.parent.stk_data['list']['kdjd'][k])
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            if pd.isnull(dea):
                i += 1
                pass
            else:
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            dea = float(self.parent.stk_data['list']['kdjd'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.painter.setBrush(QtCore.Qt.NoBrush)
            self.setPen('white')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

  #         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            dea = float(self.parent.stk_data['list']['kdjd'][first+k])

            if pd.isnull(dea):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('white')
                self.painter.setBrush(QtCore.Qt.white)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')

        self.setPen('magan')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            dea = float(self.parent.stk_data['list']['kdjj'][k])
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            if pd.isnull(dea):
                i += 1
                pass
            else:
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            dea = float(self.parent.stk_data['list']['kdjj'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.painter.setBrush(QtCore.Qt.NoBrush)
            self.setPen('magan')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

  #         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            dea = float(self.parent.stk_data['list']['kdjj'][first+k])

            if pd.isnull(dea):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('red')
                self.painter.setBrush(QColor(255,0,255))
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)

        # draw Golden Cross
        k = 0 
        for i in range(first+1,last):
            diffp = float(self.parent.stk_data['list']['kdjk'][i-1])
            deap = float(self.parent.stk_data['list']['kdjd'][i-1])
            diff = float(self.parent.stk_data['list']['kdjk'][i])
            dea = float(self.parent.stk_data['list']['kdjd'][i])
#            macd = float(self.parent.stk_data['list']['macd'][i])
            if pd.isnull(diff) or pd.isnull(dea) or pd.isnull(diffp) or pd.isnull(deap):
                k += 1
                continue
            else:
                pass
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            y1 = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight

            self.setPen('green')
            self.painter.setBrush(QColor(64,160,64))
            
            if (diffp < deap) and ((diff > dea)): 
                #self.painter.drawText(x+4+self.widthOf_KLine/2,y1,"C")
                #self.painter.drawImage(x+self.widthOf_KLine/2-self.parent.image_down.width()/2,y1,self.parent.image_up)
                #self.painter.drawImage(x+self.widthOf_KLine/2,y1,self.parent.image_up)
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1,self.widthOf_KLine,self.parent.image_up.height()),self.parent.image_up)
            if (diffp > deap) and ((diff < dea)): 
                #self.painter.drawText(x+4+self.widthOf_KLine/2,y1,"D")
                #self.painter.drawImage(x+self.widthOf_KLine/2-self.parent.image_down.width()/2,y1-self.parent.image_down.height(),self.parent.image_down)
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1-self.parent.image_down.height(),self.widthOf_KLine,self.parent.image_down.height()),self.parent.image_down)
            k += 1

    def RSIMovingUpdate(self, index):
        if (self.parent.datacount == 0):return
       # print "macdMovingUpdate()"
        if ((self.parent.first + index) > self.parent.last):
            index = self.parent.datacount - 1 - self.parent.first

        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        str1 = "RSI(6,12,24)"
        x = self.grid_padding_left + self.left_text_padding
        y = grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6
        self.setFont('default')

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'R %.2f'%(self.parent.stk_data['list']['rsir'][self.parent.first+index])
        self.setPen('yellow')
        self.painter.drawText(x, y, str1) 

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'S %.2f'%(self.parent.stk_data['list']['rsis'][self.parent.first+index])
        self.setPen('white')
        self.painter.drawText(x, y, str1)

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'I %.2f'%(float(self.parent.stk_data['list']['rsii'][self.parent.first+index]))
        self.setPen('magan')
        self.painter.drawText(x, y, str1)

        x = self.w-self.grid_padding_right
        str1 = 'Date'
        str1 += ' '
        str1 += '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        str1 += '  '
        x -= self.painter.fontMetrics().width(str1)
        str1 = 'Date'
        self.setPen('blue')
        self.painter.drawText(x, y, str1) 
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        self.setPen('magan')
        self.painter.drawText(x, y, str1)

    def RSIGridPaint(self):
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        self.setPen('black')
        self.painter.setBrush(QtGui.QColor(24, 85, 210))
        self.painter.drawRoundRect(self.grid_padding_left+self.left_text_padding-2, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+1,self.painter.fontMetrics().width("RSI(6,12,24)")+4,self.painter.fontMetrics().height()+1)

        self.setPen('white')
        self.painter.setBrush(QtCore.Qt.NoBrush)
        self.painter.drawText(self.grid_padding_left+self.left_text_padding, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6,"RSI(6,12,24)") 

        #if (self.parent.diff_dea_range == 0 or self.parent.macd_range == 0): return
        #if (self.parent.datacount == 0):return

        self.setPen('magan')

        high = float(self.parent.RSI_max)
        low  = float(self.parent.RSI_min)
 
        limit_top = high 
        limit_low = low
         
        px_h_radio = float((limit_top-limit_low)/(grid_height*self.iratio-self.seperatorHeight))

        # draw macd range square
        self.painter.drawRect(2,self.seperatorHeight+self.grid_padding_top+(grid_height*(self.pratio+self.vratio)),self.grid_padding_left-4, grid_height*self.iratio-1-self.seperatorHeight)
        self.painter.drawRect(2,self.grid_padding_top,self.grid_padding_left-4, grid_height*self.pratio)
         
        self.setPen('gray_1px_dashline')
         
        grid_num = 2
        x = grid_num
        cnt = (grid_height*(self.iratio)-self.seperatorHeight)/grid_num
        for i in range(0,grid_num):
            self.setPen('gray_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+(grid_height*(self.pratio+self.vratio))+i*cnt+self.seperatorHeight
            x1 = self.grid_padding_left
            x2 = self.w - self.grid_padding_right
             
            # draw volume dot line
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            vol_int = '%.2f'%float(limit_low+cnt*x*px_h_radio)
            vol_str = str(vol_int)
            fw = self.painter.fontMetrics().width(vol_str) #获得文字宽度
            if i == 0:
                fh = self.painter.fontMetrics().height()   #获得文字高度
            else:
                fh = self.painter.fontMetrics().height()/2   #获得文字高度
            self.setPen('red')
            #self.painter.drawText(x2+2,y1+fh,vol_str) #写入文字
            self.painter.drawText(self.left_text_padding,y1+fh,vol_str)
        #    self.setPen('white')
        #    self.painter.drawText(x1-2-self.metrics.width(str(x)),y1+fh,str(x))    #写入文字
            x-=1

        self.setPen('red')
        fh = self.painter.fontMetrics().height()/2   #获得文字高度
        self.painter.drawText(self.left_text_padding, self.h-self.grid_padding_bottom-fh,'%.2f'%(limit_low))

    def RSI_Paint(self, first, last):
        if (self.parent.RSI_max == 0 or self.parent.RSI_min == 0): return
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
#        print "DIFF_DEA_MACD_Paint()"
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h-sum_height+1
         
        high = float(self.parent.RSI_max)
        low  = float(self.parent.RSI_min)

        limit_top = high 
        limit_low = low
         
        x1 = self.grid_padding_left
        x2 = self.w-self.grid_padding_right
             
        h_radio = (grid_height*self.iratio-self.seperatorHeight)/((limit_top-limit_low))

        self.painter.setBrush(QtCore.Qt.NoBrush)
        w = self.grid_padding_left+self.widthOf_KLine
        self.setPen('yellow')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            diff = float(self.parent.stk_data['list']['rsir'][k])
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            i += 1
            if pd.isnull(diff):
                pass
            else:
                path.moveTo(w,y)
                break
        
        for k in range(first+i, last):
            diff = float(self.parent.stk_data['list']['rsir'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.setPen('yellow')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

#         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            diff = float(self.parent.stk_data['list']['rsir'][first+k])
            if pd.isnull(diff):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('yellow')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')

        self.setPen('white')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            dea = float(self.parent.stk_data['list']['rsis'][k])
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            i += 1
            if pd.isnull(dea):
                pass
            else:
                path.moveTo(w,y)
                break

        for k in range(first+i, last):
            dea = float(self.parent.stk_data['list']['rsis'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.painter.setBrush(QtCore.Qt.NoBrush)
            self.setPen('white')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

  #         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            dea = float(self.parent.stk_data['list']['rsis'][first+k])

            if pd.isnull(dea):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('white')
                self.painter.setBrush(QtCore.Qt.white)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')

        self.setPen('magan')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            dea = float(self.parent.stk_data['list']['rsii'][k])
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            i += 1
            if pd.isnull(dea):
                pass
            else:
                path.moveTo(w,y)
                break

        for k in range(first+i, last):
            dea = float(self.parent.stk_data['list']['rsii'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.painter.setBrush(QtCore.Qt.NoBrush)
            self.setPen('magan')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

  #         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            dea = float(self.parent.stk_data['list']['rsii'][first+k])

            if pd.isnull(dea):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('red')
                self.painter.setBrush(QColor(255,0,255))
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)

        # draw Golden Cross
        k = 0 
        for i in range(first+1,last):
            diffp = float(self.parent.stk_data['list']['rsir'][i-1])
            deap = float(self.parent.stk_data['list']['rsis'][i-1])
            diff = float(self.parent.stk_data['list']['rsir'][i])
            dea = float(self.parent.stk_data['list']['rsis'][i])
#            macd = float(self.parent.stk_data['list']['macd'][i])
            if pd.isnull(diff) or pd.isnull(dea) or pd.isnull(diffp) or pd.isnull(deap):
                k += 1
                continue
            else:
                pass
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            y1 = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight

            self.setPen('green')
            self.painter.setBrush(QColor(64,160,64))
            
            if (diffp < deap) and ((diff > dea)): 
                #self.painter.drawText(x+4+self.widthOf_KLine/2,y1,"C")
                #self.painter.drawImage(x+self.widthOf_KLine/2-self.parent.image_down.width()/2,y1,self.parent.image_up)
                #self.painter.drawImage(x+self.widthOf_KLine/2,y1,self.parent.image_up)
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1,self.widthOf_KLine,self.parent.image_up.height()),self.parent.image_up)
            if (diffp > deap) and ((diff < dea)): 
                #self.painter.drawText(x+4+self.widthOf_KLine/2,y1,"D")
                #self.painter.drawImage(x+self.widthOf_KLine/2-self.parent.image_down.width()/2,y1-self.parent.image_down.height(),self.parent.image_down)
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1-self.parent.image_down.height(),self.widthOf_KLine,self.parent.image_down.height()),self.parent.image_down)
            k += 1


    def MACDGridPaint(self):
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        max_diff_dea = self.parent.diff_dea_range*2

        self.setPen('black')
        self.painter.setBrush(QtGui.QColor(24, 85, 210))
        self.painter.drawRoundRect(self.grid_padding_left+self.left_text_padding-2, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+1,self.painter.fontMetrics().width("MACD(12,26,9)")+4,self.painter.fontMetrics().height()+1)
        self.setPen('gray')
        self.setPen('white')
        self.painter.drawText(self.grid_padding_left+self.left_text_padding, grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6,"MACD(12,26,9)") 
        self.painter.setBrush(QtCore.Qt.NoBrush)

        #if (self.parent.diff_dea_range == 0 or self.parent.macd_range == 0): return
        #if (self.parent.datacount == 0):return

        self.setPen('magan')

        high = float(self.parent.diff_dea_range)
        low  = float(self.parent.diff_dea_range)*(-1)
 
        limit_top = high 
        limit_low = low
         
        px_h_radio = float(max_diff_dea/(grid_height*self.iratio-self.seperatorHeight))

        # draw macd range square
        self.painter.drawRect(2,self.seperatorHeight+self.grid_padding_top+(grid_height*(self.pratio+self.vratio)),self.grid_padding_left-4, grid_height*self.iratio-1-self.seperatorHeight)
        self.painter.drawRect(2,self.grid_padding_top,self.grid_padding_left-4, grid_height*self.pratio)
         
        self.setPen('gray_1px_dashline')
         
        grid_num = 3
        x = grid_num
        cnt = (grid_height*(self.iratio)-self.seperatorHeight)/grid_num
        for i in range(0,grid_num):
            self.setPen('gray_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+(grid_height*(self.pratio+self.vratio))+i*cnt+self.seperatorHeight
            x1 = self.grid_padding_left
            x2 = self.w - self.grid_padding_right
             
            # draw volume dot line
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            vol_int = '%.3f'%float(cnt*x*px_h_radio-self.parent.diff_dea_range)
            vol_str = str(vol_int)
            fw = self.painter.fontMetrics().width(vol_str) #获得文字宽度
            if i == 0:
                fh = self.painter.fontMetrics().height()   #获得文字高度
            else:
                fh = self.painter.fontMetrics().height()/2   #获得文字高度
            self.setPen('red')
            #self.painter.drawText(x2+2,y1+fh,vol_str) #写入文字
            self.painter.drawText(self.left_text_padding,y1+fh,vol_str)
        #    self.setPen('white')
        #    self.painter.drawText(x1-2-self.metrics.width(str(x)),y1+fh,str(x))    #写入文字
            x-=1

        self.setPen('red')
        fh = self.painter.fontMetrics().height()/2   #获得文字高度
        self.painter.drawText(self.left_text_padding, self.h-self.grid_padding_bottom-fh,'%.3f'%(limit_low))
#        self.painter.drawText(self.left_text_padding, self.h-self.grid_padding_bottom-grid_height*self.iratio-4,QtCore.QString(u'X100'))


    '''绘制成交量走势表格'''
    def VolumeGridPaint(self):
        if (self.parent.datacount == 0):return
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        max_volume = self.parent.view_max_vol
        
        self.setPen('gray')
        self.painter.drawText(self.grid_padding_left+self.left_text_padding, grid_height*self.pratio+self.grid_padding_top+self.painter.fontMetrics().height()/2+6,"VOL(5,10,20)") 
         
        #px_h_radio = max_volume/(grid_height*(1.0-self.ratio))
        px_h_radio = max_volume/(grid_height*self.vratio-self.seperatorHeight)
         
        self.setPen('gray_1px_dashline')
         
        grid_num = 2
        x = grid_num
        cnt = (grid_height*(self.vratio)-self.seperatorHeight)/grid_num
        for i in range(0,grid_num):
            self.setPen('gray_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+(grid_height*self.pratio)+i*cnt+self.seperatorHeight
            x1 = self.grid_padding_left
            x2 = self.w - self.grid_padding_right
             
            # draw volume dot line
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            vol_int = int(cnt*x*px_h_radio)
            vol_int /= 10000 
            vol_str = str(vol_int)
            fw = self.painter.fontMetrics().width(vol_str) #获得文字宽度
            fh = self.painter.fontMetrics().height()/2   #获得文字高度
            self.setPen('red')
            #self.painter.drawText(x2+2,y1+fh,vol_str) #写入文字
            self.painter.drawText(self.left_text_padding, y1+fh,vol_str)
        #    self.setPen('white')
        #    self.painter.drawText(x1-2-self.metrics.width(str(x)),y1+fh,str(x))    #写入文字
            x -= 1

        self.setPen('red')
        #self.painter.drawText(self.left_text_padding, self.h-self.grid_padding_bottom-4,QtCore.QString(u'X100'))
        self.painter.drawText(self.left_text_padding, self.h-self.grid_padding_bottom-grid_height*self.iratio-4,QtCore.QString(u'X100'))
         
    '''绘制左侧信息栏和盘口等内容'''
    def rightGridPaint(self):
        self.setPen('gray')
        #绘制信息内容之间的分割线
        _h = 0
        _x = self.w-self.grid_padding_right
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
        stockID_str = QtCore.QString(u'%s'%(self.parent.stk_info['code']))
#        self.painter.drawText(_x+self.left_text_padding*3,self.titley,stockID_str)
        self.setFont('yahei_14_bold')
        self.setPen('yellow')
        fh = self.painter.fontMetrics().height() / 2   #获得文字高度
        y = self.grid_padding_top - fh

        codecName="UTF-8"
        tc = QtCore.QTextCodec.codecForName(codecName)
        QtCore.QTextCodec.setCodecForTr(tc);
        QtCore.QTextCodec.setCodecForCStrings(tc);
        QtCore.QTextCodec.setCodecForLocale(tc);
        name_str = QtCore.QString('%s'%(self.parent.stk_info['name']))
#        self.painter.drawText(self.w-self.left_text_padding*3-self.painter.fontMetrics().width(QtCore.QString(self.parent.stk_info['name'])),self.titley,name_str)
        self.parent.titlew.setTitleData(str(stockID_str), str(name_str))

        # draw current price
        self.setFont('yahei_18_bold')
        current=float(0.0)
        if (stockID_str[0] == '1' or stockID_str[0] == '5'):
            if (self.parent.datacount>0):
                current='%4.3f'%(self.parent.stk_data['list']['close'][self.parent.datacount-1])
        else:
            if (self.parent.datacount>0):
                current='%4.2f'%(self.parent.stk_data['list']['close'][self.parent.datacount-1])
        fh = self.painter.fontMetrics().height() 
        fw = self.painter.fontMetrics().width(str(current))
        #c = self.parent.stk_data['list']['close'][self.parent.datacount-1]
        c = float(self.parent.stk_data['current_price'])
        cy = float(self.parent.stk_data['pre_close'])
        diff = float(c)-float(cy)
        if (stockID_str[0] == '1' or stockID_str[0] == '5'):
            ff = '%4.3f'%float(diff)
            diff = float('%4.3f'%float(ff))
        else:
            ff = '%4.2f'%float(diff)
            diff = float('%4.2f'%float(ff))
        percent = '%4.2f'%((diff)*100/cy)
        if (c > cy):
            self.setPen('red')
        else:
            self.setPen('green')
        cprice = float(0.0) 
        if (stockID_str[0] == '1' or stockID_str[0] == '5'):
            cprice = QtCore.QString('%4.3f'%(self.parent.stk_data['current_price']))
        else:
            cprice = QtCore.QString('%4.2f'%(self.parent.stk_data['current_price']))

        # draw current price
        #for diameter in range(0, 256, 9):
        #    delta = abs((self.parent.frameNo % 128) - diameter / 2)
        #    alpha = 255 - (delta * delta) / 4 - diameter
        #    if alpha > 0:
        #        if (c>cy):
        #            self.painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, alpha), 3))
        #        else:
        #            self.painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, alpha), 3))

        #        if self.parent.floatBased:
        #            self.painter.drawText(_x+(self.grid_padding_right-fw)/2,54,cprice)
        #        else:
        #            self.painter.drawText(_x+(self.grid_padding_right-fw)/2,54,cprice)


        #self.painter.drawText(_x+(self.grid_padding_right-fw)/2,54,str(self.parent.stk_data['list']['close'][self.parent.datacount-1]))
        ss=""
        if (c > cy):
            if (stockID_str[0] == '1' or stockID_str[0] == '5'):
                ss='%s  +%s'%(str('%4.3f'%diff),str(percent))
            else:
                ss='%s  +%s'%(str('%4.2f'%diff),str(percent))
        else:
            if (stockID_str[0] == '1' or stockID_str[0] == '5'):
                ss='%s  %s'%(str('%4.3f'%diff),str(percent))
            else:
                ss='%s  %s'%(str('%4.2f'%diff),str(percent))
        ss += "%"
        self.setFont('yahei_14_bold')
        fw=self.painter.fontMetrics().width(ss)
        _h = 50 + fh*0.8
        #for diameter in range(0, 256, 9):
        #    delta = abs((self.parent.frameNo % 128) - diameter / 2)
        #    alpha = 255 - (delta * delta) / 4 - diameter
        #    if alpha > 0:
        #        if (c>cy):
        #            self.painter.setPen(QtGui.QPen(QtGui.QColor(255, 0, 0, alpha), 3))
        #        else:
        #            self.painter.setPen(QtGui.QPen(QtGui.QColor(60, 160, 64, alpha), 3))

        #        if self.parent.floatBased:
        #            self.painter.drawText(_x + (self.grid_padding_right-fw)/2, _h, ss)
        #        else:
        #            self.painter.drawText(_x + (self.grid_padding_right-fw)/2, _h, ss)
        flag = int(0)
        if (c > cy):
            flag = int(1)
        else:
            flag = int(0)
        self.parent.flashw.setFwData(str(cprice), str(ss), int(flag))

        self.setPen('gray')
        self.setFont('yahei_14_bold')
        _h = 50 + fh*1.1
        self.painter.drawLine(self.w-self.grid_padding_right, _h, self.w-1, _h)
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

#        name_str = QtCore.QString(u'%s %s'%(self.parent.stk_info['code'],self.parent.stk_info['name']))
#        ss=QtCore.QString(u'%s %s'%

            #price = '%4.2f'%(price_float) #格式化价格成str
        #name_str = QtCore.QString(u'%s'%(self.parent.stk_info['code']))
         
        #委比和委差
        self.setPen('gray')
        self.setFont('yahei_14_bold')
        zx_str = QtCore.QString(u'最新')
        _xh = 218
        self.setPen('gray')
        self.painter.drawText(_x+self.left_text_padding,_xh,zx_str)
        self.setPen('gray')
        _h += 17
        wb_str = QtCore.QString(u'委比')
        wc_str = QtCore.QString(u'委差')
        self.painter.drawLine(_x, _h+6, self.w-1, _h+6)
        xs_str = QtCore.QString(u'现手')
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

        c = float(self.parent.stk_data['current_price'])
        cy = float(self.parent.stk_data['pre_close'])
         
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
            self.painter.drawText(_x + self.left_text_padding + self.metrics.width(str(sell_str))*1.5, 62 + (i*18) + _sh, QtCore.QString(str(self.parent.stk_data['list']['sell_p'][4-i])))
            self.setPen('yellow')
            vstring = QtCore.QString(str(self.parent.stk_data['list']['sell_v'][4-i]))
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
            self.painter.drawText(_x + self.left_text_padding + self.metrics.width(str(buy_str))*1.5, 87 + (i*18) + _sh, QtCore.QString(str(self.parent.stk_data['list']['buy_p'][f])))
            self.setPen('yellow')
            vstring = QtCore.QString(str(self.parent.stk_data['list']['buy_v'][f]))
            self.painter.drawText(self.w - self.left_text_padding*2 - self.metrics.width(str(vstring)) - 3, 87 + (i*18) + _sh, vstring)
            f += 1
            i += 1
 
        self.setPen('gray')
        #self.setPen('red_2px')
        self.painter.drawLine(_x+1,377+_sh,_x+99,377+_sh)
        #self.painter.drawLine(_x+1,46,_x+65,46)
        #self.setPen('green_2px')
        self.painter.drawLine(_x+102,377+_sh,_x+199,377+_sh)
        #self.painter.drawLine(_x+67,46,_x+199,46)
        self.setFont('default')
         
    '''绘制左右侧的价格刻度'''
    def rulerPaint(self):
        if (self.parent.datacount == 0):return
        #self.painter.drawText(100,100,QtCore.QString(u'股价序列'))
        self.setFont('default')
#        print "font.family = ", self.painter.font().family()
        sm=QtCore.QString(u'xx四通股份市盈xx')
#        print sm.toUtf8()
        #codecName="UTF-8"
        #tc = QtCore.QTextCodec.codecForName(codecName)
        ##tc=QTextCodec::codecForName("UTF-8");
    #//o#r the code below may work, not the same as the book told in P27
    #// #   //replace "UTF-8" with "GB2312"
        #QtCore.QTextCodec.setCodecForTr(tc);
        #QtCore.QTextCodec.setCodecForCStrings(tc);
        #QtCore.QTextCodec.setCodecForLocale(tc);
#        self.painter.drawText(100,100,sm)
        sm=QtCore.QString(u'换手率,每股收益(EPS),每股净资产,净利润,总股本(万股),流通股本(万股),流通市值,市盈率')
#        self.painter.drawText(430,100,sm)
      #  QLabel label(QObject::tr("Hello World!你好，Qt!"));   //attention!! QObject::tr() used.

      #  self.painter.drawText(100,100,unicode(QtCore.QString(u'xx四通股份ff').toUtf8(),'utf-8','ignore'))
        #self.painter.drawText(100,100,(QtCore.QString(u'xx四通股份ff')))
        #self.painter.drawText(100,100,unicode(QtCore.QString(u'xx四通股份ff'),'gbk','ignore').encode('gb2312'))
        #self.painter.drawText(100,100,unicode(sm,'gbk','ignore'))
#        self.painter.drawText(100,100,QtCore.QString(sm.toUtf8()))#(sm,'gbk','ignore'))
        sum_width  = self.grid_padding_left+self.grid_padding_right
        sum_height = self.grid_padding_top+self.grid_padding_bottom
 
        grid_height = self.h-sum_height
         
        high = self.parent.price_view_high
        low  = self.parent.price_view_low
        #print "high = ", high, "low = ", low
        limit_top = high
        limit_low = low
 
        #px_h_radio = (grid_height*self.ratio)/((limit_top-limit_low)*100)
        px_h_radio = 0.0
        if ((high-low)*self.hscale > 0):
            px_h_radio = (grid_height*self.pratio)/((high-low)*self.hscale)
         
        grid_num = 3
        cnt = grid_height*self.pratio/grid_num
         
        if px_h_radio <= 0: return

        for i in range(0, grid_num):
            self.setPen('gray_1px_dashline')
            #计算坐标
            y1 = self.grid_padding_top+i*cnt
            x1 = self.grid_padding_left
            x2 = self.grid_padding_left+self.w-sum_width
             
            self.painter.drawLine(x1,y1,x2,y1) #画价位虚线
             
            price_float = (limit_top - ((i*cnt)/px_h_radio/self.hscale)) #计算价格
            price = '%4.2f'%(price_float) #格式化价格成str
             
            fw = self.painter.fontMetrics().width(price) #获得文字宽度
            fh = self.painter.fontMetrics().height()/2   #获得文字高度
            if (i == 0):
                y1 += fh
 
            self.setPen('red')
            self.painter.drawText(self.left_text_padding, y1+fh, price) #写入文字
#            self.painter.drawText(x2+40-r_fw,y1+r_fh,radio_str) #写入文字
        price = '%4.2f'%(low) #格式化价格成str
        self.painter.drawText(self.left_text_padding, grid_height*self.pratio+self.grid_padding_top-fh, price) 

    def datetime_Paint(self):
        self.setPen('yellow')
        self.painter.drawText(self.grid_padding_left + self.left_text_padding, self.grid_padding_top + 18, du.get_now())

    '''绘制x,y准星'''
    def xyPaint(self, first, last):
        if (self.parent.datacount == 0):return
        if (self.parent.datacount == 1):return
        if (first == 0 and last == 0):return
#        print "xyPaint()"
        if self.parent.m_x >= self.grid_padding_left and self.parent.m_x<=self.w-self.grid_padding_right and self.parent.m_y>=self.grid_padding_top and self.parent.m_y<=self.h-self.grid_padding_bottom and self.parent.ignore_mouse == 0 and self.parent.igm == 0:
            self.setPen('cyan_1px_dashline')
            sum_width  = self.grid_padding_left + self.grid_padding_right
            sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
            grid_height = self.h-sum_height-4
             
            high = self.parent.price_view_high
            low  = self.parent.price_view_low
 
            top = high
            bottom = low
            limit_top = top 
            limit_low = low
             
            h_radio = (grid_height*self.pratio)/((limit_top-limit_low)*self.hscale)
 
#            y = (self.h-self.grid_padding_bottom)-(self.h-sum_height)*(self.vratio)-2
            index = int(((self.parent.m_x-self.grid_padding_left)/(self.w_radio*self.widthOf_KLine)))
            #print "index = ", index
            if ((first + index) >= last):
                return
            k=index

            self.parent.current_moving_item = k

            i=first+k

            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            if (i>self.parent.datacount-1):
                return
            c=self.parent.stk_data['list']['close'][i]
            o=self.parent.stk_data['list']['open'][i]
            htop=self.parent.stk_data['list']['high'][i]
            hlow=self.parent.stk_data['list']['low'][i]
            ss = ""
            y2=0
            y1=0
            hh=0
            if c > o:
                y1 = (limit_top-c)*self.hscale*h_radio+self.grid_padding_top
                hh = (c-o)*self.hscale*h_radio 
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')
                ss = "red"
            else:
                y1 = (limit_top-o)*self.hscale*h_radio+self.grid_padding_top
                hh = (o-c)*self.hscale*h_radio 
                self.setPen('green')
                self.painter.setBrush(QColor(64,160,64))
                ss = "green"
#            self.painter.drawRect(x+4,y1+1,self.widthOf_KLine,hh)
#           aw keymove items
            if (c > o) and (k == self.parent.current_moving_item):
                self.setPen('maganc')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x+self.widthOf_KLine/2.0),int(y1+1-self.circleradius/2.0),self.circleradius,self.circleradius)
                self.setPen('cyan_1px_dashline')
                self.painter.drawLine(self.grid_padding_left+1,y1+1+self.circleradius/2.0,self.w-self.grid_padding_right,y1+1+self.circleradius/2.0)
            elif(c <= o) and (k == self.parent.current_moving_item):
                self.setPen('maganc')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x+self.widthOf_KLine/2.0),int(y1+1+hh-self.widthOf_KLine/2.0),self.circleradius,self.circleradius)
                self.setPen('cyan_1px_dashline')
                self.painter.drawLine(self.grid_padding_left+1,self.circleradius/2.0+y1+1+hh-self.widthOf_KLine/2.0,self.w-self.grid_padding_right,self.circleradius/2.0+y1+1+hh-self.widthOf_KLine/2.0)
            self.setPen('cyan_1px_dashline')
            self.painter.drawLine(x+self.widthOf_KLine/2.0+self.circleradius/2.0,self.grid_padding_top+1,x+self.widthOf_KLine/2.0+self.circleradius/2.0,self.h-self.grid_padding_bottom) 
                
            x1 = self.grid_padding_left
            x2 = self.w-self.grid_padding_right
            y1 = self.grid_padding_top
            y2 = self.h-self.grid_padding_bottom
#            self.painter.drawLine(x1+1,self.parent.m_y,x2-1,self.parent.m_y)
#            self.painter.drawLine(self.parent.m_x,y1+1,self.parent.m_x,y2-1)
     
    '''绘制时间轴刻度'''
    def timelinePaint(self, first, last):
        if (first == 0 and last == 0):return
         
        fw = self.painter.fontMetrics().width(u'00:00') #计算文字的宽度
         
        sum_width  = self.grid_padding_left+self.grid_padding_right
        sum_height = self.grid_padding_top+self.grid_padding_bottom
         
        grid_width = self.w-sum_width-2
         
        y1 = self.grid_padding_top
        y2 = y1+(self.h-sum_height)
 
        #时间轴中线
        self.setPen('myellow')
        x_pos = grid_width/2+self.grid_padding_left
         
        #self.painter.drawLine(x_pos,y1,x_pos,y2)
        str1 = '%s'%(self.parent.stk_data['list']['time'][int((last-first)*2/4)])
        fw = self.painter.fontMetrics().width(str1) #计算文字的宽度
        #self.painter.drawText(x_pos-fw/2, y2+12, str1) #13:00 
         
        #时间轴09点30分
        x_pos = self.grid_padding_left+self.left_text_padding
        str1 = '%s'%(self.parent.stk_data['list']['time'][first])
        self.painter.drawText(x_pos, y2+12, str1)#9:30
         
        #时间轴10点30分
        x_pos = grid_width*0.25+self.grid_padding_left
        #self.painter.drawLine(x_pos,y1,x_pos,y2)
        str1 = '%s'%(self.parent.stk_data['list']['time'][int((last-first)*1/4)])
        fw = self.painter.fontMetrics().width(str1) #计算文字的宽度
        #self.painter.drawText(x_pos-fw/2, y2+12, str1) 
 
        #时间轴14点00分
        x_pos = grid_width*0.75+self.grid_padding_left
        #self.painter.drawLine(x_pos,y1,x_pos,y2)
        str1 = '%s'%(self.parent.stk_data['list']['time'][int((last-first)*3/4)])
        fw = self.painter.fontMetrics().width(str1) #计算文字的宽度
        #self.painter.drawText(x_pos-fw/2, y2+12, str1) 
 
        #时间轴15点00分
        str1 = '%s'%(self.parent.stk_data['list']['time'][last-1])
        fw = self.painter.fontMetrics().width(str1) #计算文字的宽度
        x_pos = grid_width+self.grid_padding_left
        self.painter.drawText(x_pos-fw, y2+12, str1) 
 
        #时间虚线 by 30min
        self.setPen('gray_1px_dashline')
        self.setPen('gray')
        x_pos_array = [0.125,0.375,0.625,0.875]
        for i in x_pos_array:
            x_pos = grid_width*i+self.grid_padding_left
            self.painter.drawLine(x_pos, self.h-self.grid_padding_bottom, x_pos, self.h - 12)
            #self.painter.drawLine(x_pos,y1,x_pos,y2)

    def volMovingUpdate(self, index):
        if (self.parent.datacount == 0):return
        #print "volMovingUPdate()"
        if (self.parent.first+index>self.parent.last):
            index = self.parent.datacount - 1 - self.parent.first

        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        str1 = "VOL(5,10,20)"
        x = self.grid_padding_left + self.left_text_padding
        y = grid_height*self.pratio+self.grid_padding_top+self.painter.fontMetrics().height()/2+6
        self.setFont('default')

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'volume %d'%(self.parent.stk_data['list']['vol'][self.parent.first+index])
        self.setPen('yellow')
        self.painter.drawText(x, y, str1) 

        x = self.w-self.grid_padding_right
        str1 = 'Date'
        str1 += ' '
        str1 += '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        str1 += '  '
        x -= self.painter.fontMetrics().width(str1)

        str1 = 'Date'
        self.setPen('blue')
        self.painter.drawText(x, y, str1)
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        self.setPen('magan')
        self.painter.drawText(x, y, str1)
         
    def macdMovingUpdate(self, index):
        if (self.parent.datacount == 0):return
       # print "macdMovingUpdate()"
        if ((self.parent.first + index) > self.parent.last):
            index = self.parent.datacount - 1 - self.parent.first

        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
         
        grid_height = self.h-sum_height
        str1 = "MACD(12,26,9)"
        x = self.grid_padding_left + self.left_text_padding
        y = grid_height*(self.pratio+self.vratio)+self.grid_padding_top+self.painter.fontMetrics().height()/2+6
        self.setFont('default')

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'DIFF %.3f'%(self.parent.stk_data['list']['diff'][self.parent.first+index])
        self.setPen('yellow')
        self.painter.drawText(x, y, str1) 

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'DEA %.3f'%(self.parent.stk_data['list']['dea'][self.parent.first+index])
        self.setPen('white')
        self.painter.drawText(x, y, str1)

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = 'MACD %.3f'%(float(self.parent.stk_data['list']['macd'][self.parent.first+index])*2)
        self.setPen('green')
        self.painter.drawText(x, y, str1)

        x = self.w-self.grid_padding_right
        str1 = 'Date'
        str1 += ' '
        str1 += '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        str1 += '  '
        x -= self.painter.fontMetrics().width(str1)
        str1 = 'Date'
        self.setPen('blue')
        self.painter.drawText(x, y, str1) 
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u'  ') 
        str1 = '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        self.setPen('magan')
        self.painter.drawText(x, y, str1)


    '''绘制表格上方的股票信息'''
    def topInfoPaint(self, index):
        if (self.parent.datacount == 0):return
        if (self.parent.first+index>self.parent.last):
            index = self.parent.datacount - 1 - self.parent.first
        self.setFont('default')
        self.setPen('green')
        if (self.parent.boll == 1):
            fh = self.painter.fontMetrics().height() / 2   #获得文字高度
            y = self.grid_padding_top - fh
            x = self.grid_padding_left + self.left_text_padding
            str1 = QtCore.QString(u'BOLL')
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.painter.drawText(x, y, str1) #均价线
            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = 'UPPER %.2f'%(self.parent.stk_data['list']['boll_upper'][self.parent.first+index])
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.setPen('blue')
            self.painter.drawText(x, y, str1) #均价线

            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = 'MIDDLE %.2f'%(self.parent.stk_data['list']['boll_middle'][self.parent.first+index])
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.setPen('white')
            self.painter.drawText(x, y, str1) #均价线

            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = 'LOWER %.2f'%(self.parent.stk_data['list']['boll_lower'][self.parent.first+index])
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.setPen('magan')
            self.painter.drawText(x, y, str1) #均价线
            return

        if (self.parent.bbi == 1):
            fh = self.painter.fontMetrics().height() / 2   #获得文字高度
            y = self.grid_padding_top - fh
            x = self.grid_padding_left + self.left_text_padding
            str1 = QtCore.QString(u'BBI(3,6,12,24)')
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.painter.drawText(x, y, str1) #均价线
            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = 'BBI %.2f'%(self.parent.stk_data['list']['bbi'][self.parent.first+index])
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.setPen('yellow')
            self.painter.drawText(x, y, str1) #均价线
            return

        #fw = self.painter.fontMetrics().width(vol_str) #获得文字宽度
        fh = self.painter.fontMetrics().height() / 2   #获得文字高度
        y = self.grid_padding_top - fh
        x = self.grid_padding_left + self.left_text_padding
        str1 = QtCore.QString(u'MA')
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.painter.drawText(x, y, str1) #均价线
#        print "index = ",index

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'MA5 %.2f'%(self.parent.stk_data['list']['ma5'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.setPen('yellow')
        self.painter.drawText(x, y, str1) #均价线

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'MA10 %.2f'%(self.parent.stk_data['list']['ma10'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.setPen('white')
        self.painter.drawText(x, y, str1) #均价线

        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'MA20 %.2f'%(self.parent.stk_data['list']['ma20'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.setPen('green')
        self.painter.drawText(x, y, str1) #均价线

        if (self.parent.ktype == 'D'):
            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = 'MA30 %.2f'%(self.parent.stk_data['list']['ma30'][self.parent.first+index])
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.setPen('blue')
            self.painter.drawText(x, y, str1) #均价线

            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = 'MA60 %.2f'%(self.parent.stk_data['list']['ma60'][self.parent.first+index])
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.setPen('magan')
            self.painter.drawText(x, y, str1) #均价线

        self.setPen('myellow')
        x += 50
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'Date'
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.painter.drawText(x, y, str1) #均价线
        self.setPen('mred')
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = '%s'%(self.parent.stk_data['list']['time'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.painter.drawText(x, y, str1) #均价线

        self.setPen('myellow')
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'open'
        self.painter.drawText(x, y, str1) #均价线
        self.setPen('mred')
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = '%.2f'%(self.parent.stk_data['list']['open'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.painter.drawText(x, y, str1) #均价线

        self.setPen('myellow')
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'high'
        self.painter.drawText(x, y, str1) #均价线
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        self.setPen('mred')
        str1 = '%.2f'%(self.parent.stk_data['list']['high'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.painter.drawText(x, y, str1) #均价线

        self.setPen('myellow')
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'low'
        self.painter.drawText(x, y, str1) #均价线
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        self.setPen('mred')
        str1 = '%.2f'%(self.parent.stk_data['list']['low'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.painter.drawText(x, y, str1) #均价线

        self.setPen('myellow')
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'close'
        self.painter.drawText(x, y, str1) #均价线
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = '%.2f'%(self.parent.stk_data['list']['close'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.setPen('mred')
        self.painter.drawText(x, y, str1) #均价线

        if (self.parent.ktype == 'D'):
            self.setPen('myellow')
            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = '$'
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.painter.drawText(x, y, str1) #均价线
            x += self.painter.fontMetrics().width(str1)
            x += self.painter.fontMetrics().width(u' ') 
            str1 = '%.2f'%(self.parent.stk_data['list']['amount'][self.parent.first+index])
            sk = x + self.painter.fontMetrics().width(str1)
            if (sk > (self.w - self.grid_padding_right)):
                return
            self.setPen('mred')
            self.painter.drawText(x, y, str1) #均价线

        self.setPen('myellow')
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = 'Volume'
        self.painter.drawText(x, y, str1) #均价线
        x += self.painter.fontMetrics().width(str1)
        x += self.painter.fontMetrics().width(u' ') 
        str1 = '%d'%(self.parent.stk_data['list']['vol'][self.parent.first+index])
        sk = x + self.painter.fontMetrics().width(str1)
        if (sk > (self.w - self.grid_padding_right)):
            return
        self.setPen('mred')
        self.painter.drawText(x, y, str1) #均价线

    def DIFF_DEA_MACD_Paint(self, first, last):
        if (self.parent.diff_dea_range == 0 or self.parent.macd_range == 0): return
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
#        print "DIFF_DEA_MACD_Paint()"
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h-sum_height+1
         
        high = float(self.parent.diff_dea_range)
        low  = float(self.parent.diff_dea_range)*(-1)

        if (float(self.parent.macd_range)*2 > high):
            high = float(self.parent.macd_range)*2
            low = float(self.parent.macd_range)*(-1)*2
 
        limit_top = high 
        limit_low = low
         
        x1 = self.grid_padding_left
        x2 = self.w-self.grid_padding_right
             
        h_radio = (grid_height*self.iratio-self.seperatorHeight)/((limit_top-limit_low))

        self.setPen('green')
        ax0y = (limit_top)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
        # draw volume dot line
        self.painter.drawLine(x1,ax0y,x2,ax0y) #draw 0 aix
 
        self.painter.setBrush(QtCore.Qt.NoBrush)
        w = self.grid_padding_left+self.widthOf_KLine
        self.setPen('yellow')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            diff = float(self.parent.stk_data['list']['diff'][k])
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            i += 1
            if pd.isnull(diff):
                pass
            else:
                path.moveTo(w,y)
                break
        
        for k in range(first+i, last):
            diff = float(self.parent.stk_data['list']['diff'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.setPen('yellow')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

#         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            diff = float(self.parent.stk_data['list']['diff'][first+k])
            if pd.isnull(diff):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('maganc')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')
#            k += 1

        self.setPen('white')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            dea = float(self.parent.stk_data['list']['dea'][k])
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            i += 1
            if pd.isnull(dea):
                pass
            else:
                path.moveTo(w,y)
                break

        for k in range(first+i, last):
            dea = float(self.parent.stk_data['list']['dea'][k])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            self.painter.setBrush(QtCore.Qt.NoBrush)
            self.setPen('white')
            path.lineTo(x,y-2)
            i+=1
        self.painter.drawPath(path)

  #         draw keymove items
        if (self.parent.current_moving_item != -1):
            k = self.parent.current_moving_item
            dea = float(self.parent.stk_data['list']['dea'][first+k])

            if pd.isnull(dea):
                pass
            else:
                x1 = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
                y = (limit_top-dea)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
                self.setPen('maganc')
                self.painter.setBrush(QtCore.Qt.white)
                self.painter.drawEllipse(int(x1+self.widthOf_KLine/2.0),(y-self.circleradius*2.0/3.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')

        ax0y = (limit_top)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
        k=0 
        for i in range(first,last):
            macd=float(self.parent.stk_data['list']['macd'][i])
            macd *= 2
            if pd.isnull(macd):
                k += 1
                continue
            else:
                pass
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            y1 = (limit_top-macd)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight
            if macd > 0:
                #self.painter.setBrush(QColor(255,64,64))
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')
                ss = "red"
            else:
                self.setPen('green')
                self.painter.setBrush(QColor(64,160,64))
                ss = "green"
            # draw macd line
            if macd > 0:
                self.painter.drawLine(x+4+self.widthOf_KLine/2,y1,x+4+self.widthOf_KLine/2,ax0y)
            else:
                self.painter.drawLine(x+4+self.widthOf_KLine/2,ax0y,x+4+self.widthOf_KLine/2,y1)


#         draw keymove items
            if (macd > 0) and (k == self.parent.current_moving_item):
                self.setPen('maganc')
                if macd > 0:
                    self.painter.setBrush(QtCore.Qt.red)
                else:
                    self.painter.setBrush(QtCore.Qt.green)
                self.painter.drawEllipse(int(x+self.widthOf_KLine/2.0),(y1-self.circleradius/2),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')
            elif (k == self.parent.current_moving_item):
                self.setPen('maganc')
                if macd > 0:
                    self.painter.setBrush(QtCore.Qt.red)
                else:
                    self.painter.setBrush(QtCore.Qt.green)
                self.painter.drawEllipse(int(x+self.widthOf_KLine/2.0),(y1-self.circleradius/2),self.circleradius,self.circleradius)
                self.setPen('green')
                self.painter.setBrush(QColor(64,160,64))
                
            k += 1

        # draw Golden Cross
        k = 0 
        for i in range(first+1,last):
            diffp = float(self.parent.stk_data['list']['diff'][i-1])
            deap = float(self.parent.stk_data['list']['dea'][i-1])
            diff = float(self.parent.stk_data['list']['diff'][i])
            dea = float(self.parent.stk_data['list']['dea'][i])
            macd = float(self.parent.stk_data['list']['macd'][i])
            if pd.isnull(diff) or pd.isnull(dea) or pd.isnull(diffp) or pd.isnull(deap):
                k += 1
                continue
            else:
                pass
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            y1 = (limit_top-diff)*h_radio+self.grid_padding_top+grid_height*(self.pratio+self.vratio)+self.seperatorHeight

            self.setPen('green')
            self.painter.setBrush(QColor(64,160,64))
            
            if (diffp < deap) and ((diff > dea)): 
                #self.painter.drawText(x+4+self.widthOf_KLine/2,y1,"C")
                #self.painter.drawImage(x+self.widthOf_KLine/2-self.parent.image_down.width()/2,y1,self.parent.image_up)
                #self.painter.drawImage(x+self.widthOf_KLine/2,y1,self.parent.image_up)
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1,self.widthOf_KLine,self.parent.image_up.height()),self.parent.image_up)
            if (diffp > deap) and ((diff < dea)): 
                #self.painter.drawText(x+4+self.widthOf_KLine/2,y1,"D")
                #self.painter.drawImage(x+self.widthOf_KLine/2-self.parent.image_down.width()/2,y1-self.parent.image_down.height(),self.parent.image_down)
                self.painter.drawImage(QRect(x+8+self.widthOf_KLine,y1-self.parent.image_down.height(),self.widthOf_KLine,self.parent.image_down.height()),self.parent.image_down)
            k += 1

    def bbiPaint(self, first, last):
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h - sum_height - 2
         
        high = self.parent.price_view_high
        low  = self.parent.price_view_low
 
        limit_top = high 
        limit_low = low
         
        h_radio = (grid_height * self.pratio) / ((limit_top - limit_low) * self.hscale)
 
        self.painter.setBrush(QtCore.Qt.NoBrush)
        w = self.grid_padding_left+self.widthOf_KLine
        self.setPen('blue')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma5 = self.parent.stk_data['list']['bbi'][k]
            if pd.isnull(ma5):
                i += 1
                continue
            y = (limit_top - ma5) * self.hscale * h_radio + self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height * self.pratio + self.grid_padding_top)):
                x = (i + 1) * self.w_radio*self.widthOf_KLine + self.grid_padding_left - (self.widthOf_KLine / 2)
                path.moveTo(x,y)
                break
            i += 1
        
        factor = 3.0/4.0
        for k in range(first+i, last):
            if pd.isnull(self.parent.stk_data['list']['bbi'][k]):
                i += 1
                continue
            price = self.parent.stk_data['list']['bbi'][k]
            x = (i + 1) * self.w_radio*self.widthOf_KLine + self.grid_padding_left - (self.widthOf_KLine / 2)
            y = (limit_top - price) * self.hscale * h_radio + self.grid_padding_top
            path.lineTo(x, y + 2)
            self.setPen('magan')
            self.painter.drawEllipse(int(x),int(y),int(self.circleradius*factor),int(self.circleradius*factor))
            i += 1
        self.setPen('magan')
        self.painter.drawPath(path)

    def bollPaint(self, first, last):
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h - sum_height - 2
         
        high = self.parent.price_view_high
        low  = self.parent.price_view_low
 
        limit_top = high 
        limit_low = low
         
        h_radio = (grid_height * self.pratio) / ((limit_top - limit_low) * self.hscale)
 
        self.painter.setBrush(QtCore.Qt.NoBrush)
        w = self.grid_padding_left+self.widthOf_KLine
        self.setPen('blue')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma5 = self.parent.stk_data['list']['boll_upper'][k]
            if pd.isnull(ma5):
                i += 1
                continue
            y = (limit_top - ma5) * self.hscale * h_radio + self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height * self.pratio + self.grid_padding_top)):
                x = (i + 1) * self.w_radio*self.widthOf_KLine + self.grid_padding_left - (self.widthOf_KLine / 2)
                path.moveTo(x,y)
                break
            i += 1
        
        for k in range(first+i, last):
            price = self.parent.stk_data['list']['boll_upper'][k]
            x = (i + 1) * self.w_radio*self.widthOf_KLine + self.grid_padding_left - (self.widthOf_KLine / 2)
            y = (limit_top - price) * self.hscale * h_radio + self.grid_padding_top
            path.lineTo(x, y + 2)
            i += 1
        self.painter.drawPath(path)

        self.setPen('white')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma10 = self.parent.stk_data['list']['boll_middle'][k]
            if pd.isnull(ma10):
                i += 1
                continue
            y = (limit_top-ma10)*self.hscale*h_radio+self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height*self.pratio + self.grid_padding_top)):
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            price = self.parent.stk_data['list']['boll_middle'][k]
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-price)*self.hscale*h_radio+self.grid_padding_top
            path.lineTo(x,y+2)
            i+=1
        self.painter.drawPath(path)
         
        self.setPen('magan')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma60 = self.parent.stk_data['list']['boll_lower'][k]
            if pd.isnull(ma60):
                i += 1
                continue
            y = (limit_top-ma60)*self.hscale*h_radio+self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height*self.pratio + self.grid_padding_top)):
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1
        for k in range(first+i, last):
            current = float(self.parent.stk_data['list']['close'][k])
            price = self.parent.stk_data['list']['boll_lower'][k]
            pre_b = float(self.parent.stk_data['list']['boll_lower'][k-1])
            pre = float(self.parent.stk_data['list']['close'][k-1])
            #pre = float(self.parent.stk_data['list']['low'][k-1])
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-price)*self.hscale*h_radio+self.grid_padding_top
            path.lineTo(x,y+2)
            if (current > float(price) and (pre < pre_b)): 
                self.painter.drawImage(QRect(x-0.5*self.widthOf_KLine,y+3,self.widthOf_KLine,self.parent.image_buy.height()),self.parent.image_buy)

            if (current < float(price) and (pre > pre_b)): 
                self.painter.drawImage(QRect(x-0.5*self.widthOf_KLine,y-self.parent.image_down.height(),self.widthOf_KLine,self.parent.image_down.height()),self.parent.image_down)
            i+=1
        self.painter.drawPath(path)

         
    def maTrendPaint(self, first, last):
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h - sum_height - 2
         
        high = self.parent.price_view_high
        low  = self.parent.price_view_low
 
        limit_top = high 
        limit_low = low
         
        if (limit_top != limit_low):
            h_radio = (grid_height * self.pratio) / ((limit_top - limit_low) * self.hscale)
        else:
            h_radio = (grid_height * self.pratio) / (self.hscale)
 
        self.painter.setBrush(QtCore.Qt.NoBrush)
        w = self.grid_padding_left+self.widthOf_KLine
        self.setPen('yellow')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma5 = self.parent.stk_data['list']['ma5'][k]
            if pd.isnull(ma5):
                i += 1
                continue
            y = (limit_top - ma5) * self.hscale * h_radio + self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height * self.pratio + self.grid_padding_top)):
                x = (i + 1) * self.w_radio*self.widthOf_KLine + self.grid_padding_left - (self.widthOf_KLine / 2)
                path.moveTo(x,y)
                break
            i += 1
        
        for k in range(first+i, last):
            price = self.parent.stk_data['list']['ma5'][k]
            x = (i + 1) * self.w_radio*self.widthOf_KLine + self.grid_padding_left - (self.widthOf_KLine / 2)
            y = (limit_top - price) * self.hscale * h_radio + self.grid_padding_top
            path.lineTo(x, y + 2)
            i += 1
        self.painter.drawPath(path)

        self.setPen('white')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma10 = self.parent.stk_data['list']['ma10'][k]
            if pd.isnull(ma10):
                i += 1
                continue
            y = (limit_top-ma10)*self.hscale*h_radio+self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height*self.pratio + self.grid_padding_top)):
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            price = self.parent.stk_data['list']['ma10'][k]
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-price)*self.hscale*h_radio+self.grid_padding_top
            path.lineTo(x,y+2)
            i+=1
        self.painter.drawPath(path)
         
        self.setPen('green')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma20 = self.parent.stk_data['list']['ma20'][k]
            if pd.isnull(ma20):
                i += 1
                continue
            y = (limit_top-ma20)*self.hscale*h_radio+self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height*self.pratio + self.grid_padding_top)):
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            price = self.parent.stk_data['list']['ma20'][k]
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-price)*self.hscale*h_radio+self.grid_padding_top
            path.lineTo(x,y+2)
            i+=1
        self.painter.drawPath(path)

        self.setPen('blue')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma30 = self.parent.stk_data['list']['ma30'][k]
            if pd.isnull(ma30):
                i += 1
                continue
            y = (limit_top-ma30)*self.hscale*h_radio+self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height*self.pratio + self.grid_padding_top)):
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1

        for k in range(first+i, last):
            price = self.parent.stk_data['list']['ma30'][k]
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-price)*self.hscale*h_radio+self.grid_padding_top
            path.lineTo(x,y+2)
            i+=1
        self.painter.drawPath(path)

        self.setPen('magan')
        path = QtGui.QPainterPath()
        i = 0
        for k in range(first, last):
            ma60 = self.parent.stk_data['list']['ma60'][k]
            if pd.isnull(ma60):
                i += 1
                continue
            y = (limit_top-ma60)*self.hscale*h_radio+self.grid_padding_top
            if (y > self.grid_padding_top and y < (grid_height*self.pratio + self.grid_padding_top)):
                x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
                path.moveTo(x,y)
                break
            i += 1
        for k in range(first+i, last):
            price = self.parent.stk_data['list']['ma60'][k]
            x = (i+1)*self.w_radio*self.widthOf_KLine+self.grid_padding_left-self.widthOf_KLine/2
            y = (limit_top-price)*self.hscale*h_radio+self.grid_padding_top
            path.lineTo(x,y+2)
            i+=1
        self.painter.drawPath(path)

    def chanlunPaint(self,first,last):
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h-sum_height-4
         
        high = self.parent.price_view_high
        low  = self.parent.price_view_low
        #print "high = ", high, "low = ", low
 
        top = high
        bottom = low
        limit_top = top 
        limit_low = low
         
        if (limit_top != limit_low):
            h_radio = (grid_height*self.pratio)/((limit_top-limit_low)*self.hscale)
        else:
            h_radio = (grid_height*self.pratio)/(self.hscale)
        k=0 
        fenxing = {}
        fenxing['xpos']   = []
        fenxing['value']  = []
        fenxing['type'] = []
        ding = 1
        di = 1
        iding = [] 
        vding = []
        idi = [] 
        vdi = []
        pp = -1
        i = first
        for i in range(first,last):
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            c=self.parent.stk_data['list']['close'][i]
            o=self.parent.stk_data['list']['open'][i]
            htop=self.parent.stk_data['list']['high'][i]
            hlow=self.parent.stk_data['list']['low'][i]
            ss = ""
            y2=0
            y1=0
            hh=0
            if (i)>first and (i+1)<last:
                if self.parent.stk_data['list']['high'][i] >= self.parent.stk_data['list']['high'][i-1] and self.parent.stk_data['list']['high'][i] >= self.parent.stk_data['list']['high'][i+1]:# and ding == 1:
                    fenxing['xpos'].append(k)
                    fenxing['value'].append(htop)
                    fenxing['type'].append("ding")
                    #if (pp == -1):
                    #    fenxing['xpos'].append(k)
                    #    fenxing['value'].append(htop)
                    #    fenxing['type'].append("ding")
                    #    pp = i
                    #    ding = 0
                    #    di = 1
                    #elif (i-pp)>1:
                    #    fenxing['xpos'].append(k)
                    #    fenxing['value'].append(htop)
                    #    fenxing['type'].append("ding")
                if self.parent.stk_data['list']['high'][i] <= self.parent.stk_data['list']['high'][i-1] and self.parent.stk_data['list']['high'][i] <= self.parent.stk_data['list']['high'][i+1]:
                    fenxing['xpos'].append(k)
                    fenxing['value'].append(hlow)
                    fenxing['type'].append("di")
                    #jif (pp == -1):
                    #j    fenxing['xpos'].append(k)
                    #j    fenxing['value'].append(hlow)
                    #j    fenxing['type'].append("di")
                    #j    pp = i
                    #jelif (i-pp)>1:
                    #j    fenxing['xpos'].append(k)
                    #j    fenxing['value'].append(hlow)
                    #j    fenxing['type'].append("di")
                    #j    pp = i
            k += 1

        self.setPen('blue')
        self.painter.setBrush(QtCore.Qt.blue)
        path = QtGui.QPainterPath()
        ding=1
        di = 1
        for i in range(0,len(fenxing['xpos'])):
            x = (int(fenxing['xpos'][i]))*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            htop=float(fenxing['value'][i])#self.parent.stk_data['list']['high'][i]
            hlow=float(fenxing['value'][i])#self.parent.stk_data['list']['low'][i]
            types=str(fenxing['type'][i])
            if types == "ding" and ding == 1:
        #        self.painter.drawImage(QRect(x+6+self.widthOf_KLine/2-self.parent.image_down.width()/2,(limit_top-htop)*self.hscale*h_radio+self.grid_padding_top+2-self.parent.image_down.height(),self.widthOf_KLine,self.parent.image_down.height()),self.parent.image_down)
                self.painter.drawEllipse(int(x+6+self.widthOf_KLine/2-self.circleradius/2.0),int((limit_top-htop)*self.hscale*h_radio+self.grid_padding_top+2),self.circleradius/2.0,self.circleradius/2.0)
                ding = 0
                di = 1
                if i == 0:
                    path.moveTo(int(x+6+self.widthOf_KLine/2-self.circleradius/4.0),int((limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top+2+self.circleradius/4.0))
                else:
                    path.lineTo(int(x+6+self.widthOf_KLine/2-self.circleradius/4.0),int((limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top+2+self.circleradius/4.0))

            if types == "di" and di == 1:
        #        self.painter.drawImage(QRect(x+6+self.widthOf_KLine/2-self.parent.image_up.width()/2,(limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top+2+self.circleradius,self.widthOf_KLine,self.parent.image_up.height()),self.parent.image_up)
                self.painter.drawEllipse(int(x+6+self.widthOf_KLine/2-self.circleradius/2.0),int((limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top+2),self.circleradius/2.0,self.circleradius/2.0)
                ding = 1
                di = 0
                if i == 0:
                    path.moveTo(int(x+6+self.widthOf_KLine/2-self.circleradius/4.0),int((limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top+2+self.circleradius/4.0))
                else:
                    path.lineTo(int(x+6+self.widthOf_KLine/2-self.circleradius/4.0),int((limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top+2+self.circleradius/4.0))

        self.setPen('yellow')
        self.painter.setBrush(QtCore.Qt.NoBrush)
        self.painter.drawPath(path)


    '''绘制股价走势'''
    def KlinePaint(self,first,last):
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
        sum_width  = self.grid_padding_left + self.grid_padding_right
        sum_height = self.grid_padding_top  + self.grid_padding_bottom
 
        grid_height = self.h-sum_height-4
         
        high = self.parent.price_view_high
        low  = self.parent.price_view_low
        #print "high = ", high, "low = ", low
 
        top = high
        bottom = low
        limit_top = top 
        limit_low = low
         
        if (limit_top != limit_low):
            h_radio = (grid_height*self.pratio)/((limit_top-limit_low)*self.hscale)
        else:
            h_radio = (grid_height*self.pratio)/(self.hscale)
        #print "Kline height = ", grid_height*self.ratio
 
#        h_radio = ((self.h-sum_height-2)*self.ratio)/(high)
        #ykkk = (self.h-self.grid_padding_bottom)-(self.h-sum_height)*(self.vratio)-2
        #    y = (limit_top-price)*100*h_radio+self.grid_padding_top
        k=0 
        ding = 1
        di = 1
        iding = [] 
        vding = []
        idi = [] 
        vdi = []
        for i in range(first,last):
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
#            y2 = h_radio*self.parent.stk_data['list']['vol'][i-1]
            c=self.parent.stk_data['list']['close'][i]
            cy = 0
            if (first>1):
                cy=self.parent.stk_data['list']['close'][i-1]
            bbi=self.parent.stk_data['list']['bbi'][i]
            o=self.parent.stk_data['list']['open'][i]
            htop=self.parent.stk_data['list']['high'][i]
            hlow=self.parent.stk_data['list']['low'][i]
            ss = ""
            y2=0
            y1=0
            hh=0
            if c >= o:
                y1 = (limit_top-c)*self.hscale*h_radio+self.grid_padding_top
                hh = (c-o)*self.hscale*h_radio 
                #self.painter.setBrush(QColor(255,64,64))
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')
                ss = "red"
            else:
                y1 = (limit_top-o)*self.hscale*h_radio+self.grid_padding_top
                hh = (o-c)*self.hscale*h_radio 
                self.setPen('green')
                self.painter.setBrush(QColor(64,160,64))
                ss = "green"
        #    print "kkk index = ", i, " open = ", o, " close = ", c, "color = ", ss
            self.painter.drawRect(x+4,y1+1,self.widthOf_KLine,hh)
#         draw keymove items
            if (c > o) and (k == self.parent.current_moving_item):
                self.setPen('maganc')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x+self.widthOf_KLine/2.0),int(y1+1-self.circleradius/2.0),self.circleradius,self.circleradius)
                self.painter.setBrush(QtCore.Qt.red)
                self.setPen('red')
                #draw xy scale dot line
                self.setPen('cyan_1px_dashline')
                self.painter.drawLine(x+self.widthOf_KLine/2.0+self.circleradius/2.0,self.grid_padding_top+1,x+self.widthOf_KLine/2.0+self.circleradius/2.0,self.h-self.grid_padding_bottom-1)
                self.painter.drawLine(self.grid_padding_left+1,y1+self.circleradius/2.0+1,self.w-self.grid_padding_right-1,y1+self.circleradius/2.0+1)
            elif(c <= o) and (k == self.parent.current_moving_item):
                self.setPen('maganc')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x+self.widthOf_KLine/2.0),int(y1+1+hh-self.widthOf_KLine/2.0),self.circleradius,self.circleradius)
                self.setPen('green')
                self.painter.setBrush(QColor(64,160,64))
                #draw xy scale dot line
                self.setPen('cyan_1px_dashline')
                self.painter.drawLine(x+self.widthOf_KLine/2.0+self.circleradius/2.0,self.grid_padding_top+1,x+self.widthOf_KLine/2.0+self.circleradius/2.0,self.h-self.grid_padding_bottom-1)
                self.painter.drawLine(self.grid_padding_left+1,y1+self.circleradius/2.0+1+hh-self.widthOf_KLine/2.0,self.w-self.grid_padding_right-1,y1+self.circleradius/2.0+1+hh-self.widthOf_KLine/2.0)
                
            # draw shadow up line and shade low line
            self.painter.drawLine(x+4+self.widthOf_KLine/2,(limit_top-htop)*self.hscale*h_radio+self.grid_padding_top+2, x+4+self.widthOf_KLine/2,(limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top)

            fh=self.painter.fontMetrics().height() 
            if (c > bbi) and (cy < bbi) and (self.parent.bbi == 1):
                yy=(limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top
                self.painter.drawImage(QRect(x+0.2*self.widthOf_KLine,yy+3,self.widthOf_KLine,self.parent.image_smile.height()),self.parent.image_smile)

            self.setPen('white')
            if (htop == self.parent.price_view_high):
                fw=self.painter.fontMetrics().width(str('%4.2f'%(htop)))
                if (x+4+self.widthOf_KLine/2+fw > (self.w-self.grid_padding_right)):
                    x=self.w-self.grid_padding_right-fw-self.left_text_padding-self.widthOf_KLine 
                if (x<self.grid_padding_left+self.left_text_padding):
                    x=self.grid_padding_left+self.left_text_padding 
                self.painter.drawText(x,(limit_top-htop)*self.hscale*h_radio+self.grid_padding_top+4+self.painter.fontMetrics().height()/2,str('%4.2f'%(htop)))
            if (hlow == self.parent.price_view_low):
                fw=self.painter.fontMetrics().width(str('%4.2f'%(hlow)))
                if (x+4+self.widthOf_KLine/2+fw > (self.w-self.grid_padding_right)):
                    x=self.w-self.grid_padding_right-fw-self.left_text_padding-self.widthOf_KLine 
                if (x<self.grid_padding_left+self.left_text_padding):
                    x=self.grid_padding_left+self.left_text_padding 
                ylow=(limit_top-hlow)*self.hscale*h_radio+self.grid_padding_top
                if ((ylow+fh/2)>((grid_height*self.pratio)+self.grid_padding_top)):
                    ylow=(grid_height*self.pratio)+self.grid_padding_top-fh/2+2
                self.painter.drawText(x,ylow,str('%4.2f'%(hlow)))
            k += 1

    def ktypePaint(self):
        label = KLABEL[self.parent.ktype]
        fw = self.painter.fontMetrics().width(label)
        fh = self.painter.fontMetrics().height() 
        self.setPen('yellow')
        self.painter.drawText((self.grid_padding_left - fw) / 2.0, self.grid_padding_top - (fh / 2.0), label)
        if (self.parent.current_moving_item != -1):
            y = 40
            index = self.parent.current_moving_item
            label = "收盘价" 
            fw = self.painter.fontMetrics().width(label)
            fh = self.painter.fontMetrics().height() 
            self.setPen('white')
            self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
            if (self.parent.stockid[0] == '1' or self.parent.stockid[0] == '5'):#ETF 
                str1 = '%.3f'%(self.parent.stk_data['list']['close'][self.parent.first+index])
            else:
                str1 = '%.2f'%(self.parent.stk_data['list']['close'][self.parent.first+index])
            fw = self.painter.fontMetrics().width(str1)
            self.setPen('yellow')
            self.painter.drawText((self.grid_padding_left - fw -4), y + 18 +self.grid_padding_top - (fh / 2.0), str1)
            y += 18
            y += fh
            if (self.parent.first + index - 1 > 0):
                label = "涨   幅" 
                fw = self.painter.fontMetrics().width(label)
                fh = self.painter.fontMetrics().height() 
                self.setPen('white')
                self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
                cy = float(self.parent.stk_data['list']['close'][self.parent.first+index-1])
                c = float(self.parent.stk_data['list']['close'][self.parent.first+index])
                    
                if (self.parent.stockid[0] == '1' or self.parent.stockid[0] == '5'):#ETF 
                    str1 = '%.3f'%((float(c)-float(cy))*100.0/float(cy))
                else:
                    str1 = '%.2f'%((float(c)-float(cy))*100.0/float(cy))
                fw = self.painter.fontMetrics().width(str1)
                self.setPen('yellow')
                self.painter.drawText((self.grid_padding_left - fw -4), y + 18 + self.grid_padding_top - (fh / 2.0), str1)
                y += 18
                y += fh
            if (self.parent.stockid[0] != '1' and self.parent.stockid[0] != '5' and self.parent.ktype != 'D'):
                label = "换手率" 
                fw = self.painter.fontMetrics().width(label)
                fh = self.painter.fontMetrics().height() 
                self.setPen('white')
                self.painter.drawText((self.grid_padding_left - fw) / 2.0, y + self.grid_padding_top - (fh / 2.0), label)
                str1 = '%.2f'%(float(self.parent.stk_data['list']['turnover'][self.parent.first+index]))
                fw = self.painter.fontMetrics().width(str1)
                self.setPen('yellow')
                self.painter.drawText((self.grid_padding_left - fw -4), y + 18 + self.grid_padding_top - (fh / 2.0), str1)
                y += 18
                y += fh
#        self.painter.drawImage(QRect(300, self.grid_padding_top+1, 16, 16), self.parent.image_down)
#        self.painter.drawImage(QRect(500, self.grid_padding_top+1, 16, 16), self.parent.image_up)
         
    '''绘制成交量'''
    def volumePaint(self,first,last):
        if (self.parent.datacount == 0):return
        if (first == 0 and last == 0):return
#        print "volumePaint()"
        sum_width  = self.grid_padding_left + self.grid_padding_right
        #sum_height = self.grid_padding_top  + self.grid_padding_bottom + self.seperatorHeight
        sum_height = self.grid_padding_top  + self.grid_padding_bottom

        max_volume = self.parent.view_max_vol #最大成交量
         
        #h_radio = ((self.h-sum_height-2)*(self.vratio)-self.seperatorHeight-2)/max_volume
        h_radio = ((self.h-sum_height)*self.vratio-self.seperatorHeight-2)/max_volume
# 92 in dazhihui vol
# 151 in dazhihui macd
        #print "Volume height = ", (self.h-sum_height-2)*(1.0-self.ratio)-self.seperatorHeight-2
 
        bottom_vol = self.h-self.grid_padding_bottom-(self.h-sum_height)*self.iratio
         
        k=0
        for i in range(first,last):
            x = (k)*self.w_radio*self.widthOf_KLine+self.grid_padding_left
            y2 = h_radio*self.parent.stk_data['list']['vol'][i]
            c=self.parent.stk_data['list']['close'][i]
            o=self.parent.stk_data['list']['open'][i]
            ss = ""
            if c >= o:
                self.painter.setBrush(QtCore.Qt.red)
                #self.painter.setBrush(QColor(255,64,64))
                self.setPen('red')
                ss = "red"
            else:
                self.setPen('green')
                self.painter.setBrush(QColor(64,160,64))
                #self.painter.setBrush(QtCore.Qt.green)
                ss = "green"
        #    print "index = ", i, " open = ", o, " close = ", c, "color = ", ss
            self.painter.drawRect(x+4,bottom_vol-y2,self.widthOf_KLine,y2)

            if (k == self.parent.current_moving_item):
                self.setPen('maganc')
                self.painter.setBrush(QtCore.Qt.yellow)
                self.painter.drawEllipse(int(x+self.widthOf_KLine/2.0),int(bottom_vol-y2-self.widthOf_KLine/2.0),self.circleradius,self.circleradius)
            k += 1

        self.painter.setBrush(QtCore.Qt.NoBrush)

class Reading_Thread(QtCore.QThread):

    renderedImage = QtCore.pyqtSignal(str)
    updateSdata = QtCore.pyqtSignal(dict)

    def __init__(self, stockid, parent=None):
        super(Reading_Thread, self).__init__(parent)
        self.sid = stockid
        self.abort = False
        self.thread_running = 1
        self.thread_finished = 0
    
    def __del__(self):
        print "thread delete"
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
            except Exception as e:
                print "failed to get_realtime from website"
                print(e)

            if (self.thread_running == 0):
                self.thread_finished = 1
                return

            try:
                df = ts.get_today_ticks(self.sid)
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
                else:  
                    print "failed to get_today_tick()"
            except Exception as e:
                print "failed to get_today_ticks from website"
                print(e)

            try:
                # get pankou data from website
                if (self.sid[0] == '1' or self.sid[0] == '5'):#for ETF or B funds
                    indicators.get_trade_history(self.sid, 'D')
                    indicators.get_trade_history(self.sid, 'W')
                    indicators.get_trade_history(self.sid, 'M')
                else:
                    indicators.get_trade_history(self.sid, '5')
                    indicators.get_trade_history(self.sid, '15')
                    indicators.get_trade_history(self.sid, '30')
                    indicators.get_trade_history(self.sid, '60')
                    indicators.get_trade_history(self.sid, 'D')
                    indicators.get_trade_history(self.sid, 'W')
                    indicators.get_trade_history(self.sid, 'M')
            except Exception as e:
                print "failed to get_realtime from website"
                print(e)

            if (self.thread_running == 0):
                self.thread_finished = 1
                return
            print "QThread Reading_Thread"
            self.renderedImage.emit(str(time.strftime('%Y-%m-%d %H:%M:%S')))
#            self.updateSdata.emit
            time.sleep(2)
        self.thread_finished = 1
 
class KHistoryWidget(QtGui.QWidget):
    def __init__(self, path, parent=None):
        QtGui.QWidget.__init__(self, parent)
        #self.swidth = QApplication.desktop().screenGeometry().width()
        #self.sheight = QApplication.desktop().screenGeometry().height()
        self.swidth = QApplication.desktop().availableGeometry().width()
        self.sheight = QApplication.desktop().availableGeometry().height()
        #print "www = ", self.swidth, " hhh = ", self.sheight
        self.setMinimumSize(998, 388) #设置窗口最小尺寸
        self.setGeometry(0, 0, self.swidth, self.sheight)
        self.setWindowTitle(QtCore.QString(u'Stouck hunter'))
        self.setStyleSheet("QWidget { background-color: black}")
        self.setWindowIcon(QtGui.QIcon('ruby.png'))
        self.setMouseTracking(True)
        self.m_x = 0 #光标x轴位置
        self.m_y = 0 #光标y轴位置
        self.seq = 0

        self.image_down = QtGui.QImage()
        if not self.image_down.load('../images/down.png'):
            print "load failed!!!"
        self.image_up = QtGui.QImage()
        if not self.image_up.load('../images/up.png'):
            print "load failed!!!"
        self.image_buy = QtGui.QImage()
        if not self.image_buy.load('../images/buy.png'):
            print "load failed!!!"
        self.image_smile = QtGui.QImage()
        if not self.image_smile.load('../images/smile.png'):
            print "load failed!!!"

        self.floatBased = False
        self.frameNo = 0
        self.datacount = 0

        self.price_view_high = 0.0
        self.price_view_low = 0.0
        self.view_max_vol = 0 
        self.diff_dea_range = 0.0
        self.macd_range = 0.0
        self.KDJ_max = 0.0
        self.KDJ_min = 0.0
        self.RSI_max = 0.0
        self.RSI_min = 0.0
        self.DZX_max = 0.0
        self.DZX_min = 0.0

        self.stockid = path
        self.stockid += "/"
        #print self.stockid
        mycode = locale.getpreferredencoding()
        #print "font.family = ", QtGui.QPainter().font().family()
         
        self.stk_info = {}
         
        self.stk_info['code'] = path[0:6]
#        self.stk_info['market'] = 'SH'
         
        self.stk_data = {}
        self.stk_data['list'] = {} #股价序列
        self.initialize_history()
        self.stk_data['current_price'] = 0.0
         
#        self.stk_data['list']['buy_port'] = [(0.00,0),(0.00,0),(0.00,0),(0.00,0),(0.00,0)]  #买盘前五
#        self.stk_data['list']['sell_port'] = [(0.00,0),(0.00,0),(0.00,0),(0.00,0),(0.00,0)] #卖盘前五
        self.stk_data['list']['buy_v']  = []  #买盘前五
        self.stk_data['list']['buy_p']  = []  #买盘前五
         
        self.stk_data['list']['sell_v'] = []  #买盘前五
        self.stk_data['list']['sell_p'] = []  #买盘前五
        
        self.ktype = 'E'

        #读取数据
        self.load_trade_history()
        # load readtime data
        self.load_realtime_data()

        self.flashw = FlashWidget(str(""), str(""), 0, self)
        self.titlew = TitleWidget(str(""), str(""), self)
        
        self.maxWidthOfKline = 12 

        self.first = 0
        if (self.datacount > DEFAULT_DAYS):
            self.first = (self.datacount - DEFAULT_DAYS)
        self.last = len(self.stk_data['list']['time'])
        self.current_moving_item = -1 
        self.ignore_mouse = 1
        self.dumpMACD()

        self.updateRange()
        self.setCursor(Qt.CrossCursor)

        self.igm = 1

        self.macdflag = 1
        self.kdjflag = 0
        self.rsiflag = 0
        self.dzxflag = 0
        self.sma = 0
        self.bbi = 1
        self.boll = 0
         
        self.installEventFilter(self)
        self.flashw.installEventFilter(self)
        
        self.thread = Reading_Thread(self.stk_info['code'])

        self.thread.renderedImage.connect(self.refresh_stock_data)
        self.thread.start()

        datetime_timer = QtCore.QTimer(self)
        datetime_timer.timeout.connect(self.datetime_update_timer)
        datetime_timer.start(1000)
    
    def terminal(self):
        while (not self.thread.isFinished()):
            self.thread.quit()
            self.thread.terminate()
            self.thread.exit(0)

    def refresh_stock_data(self, text):
        print self.seq, "refresh_stock_data", text
     	self.load_trade_history()
        self.load_realtime_data()
        self.update()

    def load_ETF_history(self):
        ss = self.stockid
        ss += K_FILE_NAME[self.ktype]
        f = open(self.stockid + K_FILE_NAME[self.ktype], 'r')
        data = f.readlines()
        f.close()
        if len(data) == 0 or len(data) == 1: return
        if len(data) == 2:
            i = 0
            for row in data:
                if i == 0: 
                    i += 1
                    continue
                vars = row.split(',')
                self.stk_data['list']['time'].insert(0, vars[0])
                self.stk_data['list']['open'].insert(0, float(vars[1]))   	
                self.stk_data['list']['high'].insert(0, float(vars[2]))   	
                self.stk_data['list']['close'].insert(0, float(vars[3]))  
                self.stk_data['list']['low'].insert(0, float(vars[4]))   
                self.stk_data['list']['vol'].insert(0, float(vars[5]))  
                self.stk_data['list']['mprice_change'].insert(0, float(vars[6])) 
                self.stk_data['list']['mp_change'].insert(0, float(vars[7]))
                self.stk_data['list']['ma5'].insert(0, float(vars[8]))
                self.stk_data['list']['ma10'].insert(0,float( vars[9]))
                self.stk_data['list']['ma20'].insert(0, float(vars[10]))
                self.stk_data['list']['v_ma5'].insert(0, float(vars[11]))
                self.stk_data['list']['v_ma10'].insert(0, float(vars[12]))
                self.stk_data['list']['v_ma20'].insert(0, float(vars[13]))
                self.stk_data['list']['turnover'].insert(0, float(vars[14]))
                self.stk_data['list']['diff']   	= []   
                self.stk_data['list']['dea']    	= []   
                self.stk_data['list']['macd']   	= []  
                self.stk_data['list']['amount'] 	= [] #成交额
                self.stk_data['list']['mma']    	= []   #分时均价
#rsi
                self.stk_data['list']['rsir']   	= []   
                self.stk_data['list']['rsis']    	= []   
                self.stk_data['list']['risi']   	= []  
#kdj
                self.stk_data['list']['kdjk']   	= []   
                self.stk_data['list']['kdjd']    	= []   
                self.stk_data['list']['kdjj']    	= []   

                self.stk_data['list']['bbi']    	= []   

                self.stk_data['list']['ak']    	= []   
                self.stk_data['list']['ad1']   	= []   
                self.stk_data['list']['aj']    	= []   

                self.stk_data['list']['boll_middle']    	= []   
                self.stk_data['list']['boll_upper']   	= []   
                self.stk_data['list']['boll_lower']    	= []   

                sum_vol = sum(self.stk_data['list']['vol'])
                sum_amt = sum(self.stk_data['list']['amount'])
 
                self.stk_data['list']['mma'].insert(0, float(sum_amt)/(sum_vol))

                self.load_MACD()
                self.load_RSI()
                self.load_KDJ()
                self.load_BBI()
                self.load_DZX()
                self.load_BOLL()

                lll = len(self.stk_data['list']['diff'])
                     
#                self.stk_data['pre_close'] = 0.0 #上一个交易日收盘价
                self.stk_data['open'] = self.stk_data['list']['open'][0]       #开盘价
                self.stk_data['high'] = max(self.stk_data['list']['high'])     #最高价
                self.stk_data['low']  = min(self.stk_data['list']['low'])      #最低价
                self.stk_data['close']= self.stk_data['list']['close'][-1]     #收盘价
                self.stk_data['max_vol']  = max(self.stk_data['list']['vol'])  #当日最高成交量

                self.load_SMA()

            self.datacount = len(self.stk_data['list']['open'])
            return

        f = open(self.stockid + K_FILE_NAME[self.ktype], 'r')
        f.readline()
        need2update = 0 
        date,opens,high,close,low,volume,price_change,p_change,ma5,ma10,ma20,v_ma5,v_ma10,v_ma20= \
        np.loadtxt(f, delimiter = ",", unpack = True, dtype = [\
			           ('date', '|S32'), 		\
			           ('opens', np.float),		\
			           ('high', np.float), 		\
			           ('close', np.float),		\
			           ('low', np.float), 		\
			           ('volume', np.float), 		\
			           ('price_change', np.float),	\
			           ('p_change', np.float), 	\
			           ('ma5', np.float), 		\
			           ('ma10', np.float), 		\
			           ('ma20', np.float), 		\
			           ('v_ma5', np.float), 	\
			           ('v_ma10', np.float), 	\
			           ('v_ma20', np.float)])
        f.close()
        #revers array
        mdate 		= date[ : :-1]
        mopen 		= opens[ : :-1]
        mhigh 		= high[ : :-1]
        mclose 		= close[ : :-1]
        mlow 		= low[ : :-1]
        mvolume 	= volume[ : :-1]
        mprice_change 	= price_change[ : :-1]
        mp_change	= p_change[ : :-1]
        mma5 		= ma5[ : :-1]
        mma10 		= ma10[ : :-1]
        mma20 		= ma20[ : :-1]
        mv_ma5 		= v_ma5[ : :-1]
        mv_ma10 	= v_ma10[ : :-1]
        mv_ma20 	= v_ma20[ : :-1]
#        date, opens, close = np.loadtxt("tnp", delimiter = ",", unpack = True, dtype = [('date', '|S32'), ('close', np.float), ('open', np.float)])
        #date,opens,close=np.loadtxt("tnp",delimiter=",",unpack=True,dtype=[('', '|S10'), ('', np.float),('', np.float)])
        #fff = date[ : :-1] # reverse array
        for i in range(mdate.shape[0]):
        #    kkk.append(i)
        #    rclose.insert(0, float(close[i]))
#            print mdate[i],",",float(mopen[i]),",",float(mhigh[i]),",",float(mclose[i]), ",",float(mlow[i]),",",float(mvolume[i]),float(mprice_change[i]),",",float(mp_change[i]),",",float(mma5[i]),",",float(mma10[i]),",",float(mma20[i]),",",float(mv_ma5[i]),",",float(mv_ma10[i]),",",float(mv_ma20[i]),",",float(mturnover[i])
            time = mdate[i]
            try:
                index = self.stk_data['list']['time'].index(time)
                # if index already exist, then continue
                continue
            except Exception as e:
#                print "new time stock data to be inserted." 
                need2update = 1
                #print(e)

        if (need2update == 0):
            return

        self.stk_data['list']['time']   	= mdate.tolist()
        self.stk_data['list']['open']   	= mopen.tolist()
        self.stk_data['list']['high']   	= mhigh.tolist()
        self.stk_data['list']['close']  	= mclose.tolist()
        self.stk_data['list']['low']    	= mlow.tolist()
        self.stk_data['list']['vol']    	= mvolume.tolist()
        self.stk_data['list']['mprice_change'] 	= mprice_change.tolist()
        self.stk_data['list']['mp_change']   	= mp_change.tolist()
        self.stk_data['list']['ma5']    	= mma5.tolist()
        self.stk_data['list']['ma10']   	= mma10.tolist()
        self.stk_data['list']['ma20']   	= mma20.tolist()
        self.stk_data['list']['v_ma5']  	= mv_ma5.tolist()
        self.stk_data['list']['v_ma10'] 	= mv_ma10.tolist()
        self.stk_data['list']['v_ma20'] 	= mv_ma20.tolist()
        self.stk_data['list']['diff']   	= []   
        self.stk_data['list']['dea']    	= []   
        self.stk_data['list']['macd']   	= []  
        self.stk_data['list']['amount'] 	= [] #成交额
        self.stk_data['list']['mma']    	= []   #分时均价
#rsi
        self.stk_data['list']['rsir']   	= []   
        self.stk_data['list']['rsis']    	= []   
        self.stk_data['list']['risi']   	= []  
#kdj
        self.stk_data['list']['kdjk']   	= []   
        self.stk_data['list']['kdjd']    	= []   
        self.stk_data['list']['kdjj']    	= []   

        self.stk_data['list']['bbi']    	= []   

        self.stk_data['list']['boll_middle']    	= []   
        self.stk_data['list']['boll_upper']   	= []   
        self.stk_data['list']['boll_lower']    	= []   

        self.stk_data['list']['ak']    	= []   
        self.stk_data['list']['ad1']   	= []   
        self.stk_data['list']['aj']    	= []   

        sum_vol = sum(self.stk_data['list']['vol'])
        sum_amt = sum(self.stk_data['list']['amount'])
 
        self.stk_data['list']['mma'].insert(0, float(sum_amt)/(sum_vol))

        self.load_MACD()
        self.load_RSI()
        self.load_KDJ()
        self.load_BBI()
        self.load_DZX()
        self.load_BOLL()

        #print "diff", self.stk_data['list']['diff']
        #print "dea", self.stk_data['list']['dea']
        #print "macd", self.stk_data['list']['macd']
        lll = len(self.stk_data['list']['diff'])
             
#        self.stk_data['pre_close'] = 0.0 #上一个交易日收盘价
        self.stk_data['open'] = self.stk_data['list']['open'][0]       #开盘价
        self.stk_data['high'] = max(self.stk_data['list']['high'])     #最高价
        self.stk_data['low']  = min(self.stk_data['list']['low'])      #最低价
        self.stk_data['close']= self.stk_data['list']['close'][-1]     #收盘价
        self.stk_data['max_vol']  = max(self.stk_data['list']['vol'])  #当日最高成交量

        self.load_SMA()
#        self.stk_data['list']['ma5'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=5).tolist() 
#        self.stk_data['list']['ma10'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=10).tolist() 
#        self.stk_data['list']['ma20'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=20).tolist() 
#        self.stk_data['list']['ma30'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=30).tolist() 
#        self.stk_data['list']['ma60'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=60).tolist() 
#
        self.datacount = len(self.stk_data['list']['open'])

    def load_minutes_history(self):
        f = open(self.stockid + K_FILE_NAME[self.ktype], 'r')
        data = f.readlines()
        f.close()
        if len(data) == 0 or len(data) == 1: return
        if len(data) == 2:
            i = 0
            for row in data:
                if i == 0: 
                    i += 1
                    continue
                vars = row.split(',')
                self.stk_data['list']['time'].insert(0, vars[0])
                self.stk_data['list']['open'].insert(0, float(vars[1]))   	
                self.stk_data['list']['high'].insert(0, float(vars[2]))   	
                self.stk_data['list']['close'].insert(0, float(vars[3]))  
                self.stk_data['list']['low'].insert(0, float(vars[4]))   
                self.stk_data['list']['vol'].insert(0, float(vars[5]))  
                self.stk_data['list']['mprice_change'].insert(0, float(vars[6])) 
                self.stk_data['list']['mp_change'].insert(0, float(vars[7]))
                self.stk_data['list']['ma5'].insert(0, float(vars[8]))
                self.stk_data['list']['ma10'].insert(0,float( vars[9]))
                self.stk_data['list']['ma20'].insert(0, float(vars[10]))
                self.stk_data['list']['v_ma5'].insert(0, float(vars[11]))
                self.stk_data['list']['v_ma10'].insert(0, float(vars[12]))
                self.stk_data['list']['v_ma20'].insert(0, float(vars[13]))
                self.stk_data['list']['turnover'].insert(0, float(vars[14]))
                self.stk_data['list']['diff']   	= []   
                self.stk_data['list']['dea']    	= []   
                self.stk_data['list']['macd']   	= []  
                self.stk_data['list']['amount'] 	= [] #成交额
                self.stk_data['list']['mma']    	= []   #分时均价
#rsi
                self.stk_data['list']['rsir']   	= []   
                self.stk_data['list']['rsis']    	= []   
                self.stk_data['list']['risi']   	= []  
#kdj
                self.stk_data['list']['kdjk']   	= []   
                self.stk_data['list']['kdjd']    	= []   
                self.stk_data['list']['kdjj']    	= []   

                self.stk_data['list']['bbi']   	= []   
                self.stk_data['list']['ak']    	= []   
                self.stk_data['list']['ad1']   	= []   
                self.stk_data['list']['aj']    	= []   
                self.stk_data['list']['boll_middle']    	= []   
                self.stk_data['list']['boll_upper']   	= []   
                self.stk_data['list']['boll_lower']    	= []   

                sum_vol = sum(self.stk_data['list']['vol'])
                sum_amt = sum(self.stk_data['list']['amount'])
 
                self.stk_data['list']['mma'].insert(0, float(sum_amt)/(sum_vol))

                self.load_MACD()
                self.load_RSI()
                self.load_KDJ()
                self.load_BBI()
                self.load_DZX()
                self.load_BOLL()

                lll = len(self.stk_data['list']['diff'])
                     
#                self.stk_data['pre_close'] = 0.0 #上一个交易日收盘价
                self.stk_data['open'] = self.stk_data['list']['open'][0]       #开盘价
                self.stk_data['high'] = max(self.stk_data['list']['high'])     #最高价
                self.stk_data['low']  = min(self.stk_data['list']['low'])      #最低价
                self.stk_data['close']= self.stk_data['list']['close'][-1]     #收盘价
                self.stk_data['max_vol']  = max(self.stk_data['list']['vol'])  #当日最高成交量

                self.load_SMA()

            self.datacount = len(self.stk_data['list']['open'])
            self.datacount = 1
            print "cccc=",self.datacount
            return
        
        ss = self.stockid
        ss += K_FILE_NAME[self.ktype]
        f = open(self.stockid + K_FILE_NAME[self.ktype], 'r')
        f.readline()
        need2update = 0 
        #date,opens,high,close,low,volume,price_change,p_change,ma5,ma10,ma20,v_ma5,v_ma10,v_ma20,turnover = \
        #np.loadtxt(f, delimiter = ",", unpack = True, dtype = [\
#			           ('date', '|S32'), 		\
#			           ('opens', np.float),		\
#			           ('high', np.float), 		\
#			           ('close', np.float),		\
#			           ('low', np.float), 		\
#			           ('volume', np.float), 		\
#			           ('price_change', np.float),	\
#			           ('p_change', np.float), 	\
#			           ('ma5', np.float), 		\
#			           ('ma10', np.float), 		\
#			           ('ma20', np.float), 		\
#			           ('v_ma5', np.float), 	\
#			           ('v_ma10', np.float), 	\
#			           ('v_ma20', np.float), 	\
#			           ('turnover', np.float)])
        date,opens,high,close,low,volume,price_change,p_change,ma5,ma10,ma20,v_ma5,v_ma10,v_ma20= \
        np.loadtxt(f, delimiter = ",", unpack = True, dtype = [\
			           ('date', '|S32'), 		\
			           ('opens', np.float),		\
			           ('high', np.float), 		\
			           ('close', np.float),		\
			           ('low', np.float), 		\
			           ('volume', np.float), 		\
			           ('price_change', np.float),	\
			           ('p_change', np.float), 	\
			           ('ma5', np.float), 		\
			           ('ma10', np.float), 		\
			           ('ma20', np.float), 		\
			           ('v_ma5', np.float), 	\
			           ('v_ma10', np.float), 	\
			           ('v_ma20', np.float)])
        f.close()
        #print "shape[0]",(date.shape[0])
        #revers array
     #   if (date.shape[0] > 1):
        mdate 		= date[ : :-1]
        mopen 		= opens[ : :-1]
        mhigh 		= high[ : :-1]
        mclose 		= close[ : :-1]
        mlow 		= low[ : :-1]
        mvolume 	= volume[ : :-1]
        mprice_change 	= price_change[ : :-1]
        mp_change	= p_change[ : :-1]
        mma5 		= ma5[ : :-1]
        mma10 		= ma10[ : :-1]
        mma20 		= ma20[ : :-1]
        mv_ma5 		= v_ma5[ : :-1]
        mv_ma10 	= v_ma10[ : :-1]
        mv_ma20 	= v_ma20[ : :-1]
#        mturnover 	= turnover[ : :-1] 
#        date, opens, close = np.loadtxt("tnp", delimiter = ",", unpack = True, dtype = [('date', '|S32'), ('close', np.float), ('open', np.float)])
        #date,opens,close=np.loadtxt("tnp",delimiter=",",unpack=True,dtype=[('', '|S10'), ('', np.float),('', np.float)])
        #fff = date[ : :-1] # reverse array
        for i in range(mdate.shape[0]):
        #    kkk.append(i)
        #    rclose.insert(0, float(close[i]))
#            print mdate[i],",",float(mopen[i]),",",float(mhigh[i]),",",float(mclose[i]), ",",float(mlow[i]),",",float(mvolume[i]),float(mprice_change[i]),",",float(mp_change[i]),",",float(mma5[i]),",",float(mma10[i]),",",float(mma20[i]),",",float(mv_ma5[i]),",",float(mv_ma10[i]),",",float(mv_ma20[i]),",",float(mturnover[i])
            time = mdate[i]
            try:
                index = self.stk_data['list']['time'].index(time)
                # if index already exist, then continue
                continue
            except Exception as e:
#                print "new time stock data to be inserted." 
                need2update = 1
                #print(e)

        if (need2update == 0):
            return

        self.stk_data['list']['time']   	= mdate.tolist()
        self.stk_data['list']['open']   	= mopen.tolist()
        self.stk_data['list']['high']   	= mhigh.tolist()
        self.stk_data['list']['close']  	= mclose.tolist()
        self.stk_data['list']['low']    	= mlow.tolist()
        self.stk_data['list']['vol']    	= mvolume.tolist()
        self.stk_data['list']['mprice_change'] 	= mprice_change.tolist()
        self.stk_data['list']['mp_change']   	= mp_change.tolist()
        self.stk_data['list']['ma5']     	= mma5.tolist()
        self.stk_data['list']['ma10']   	= mma10.tolist()
        self.stk_data['list']['ma20']   	= mma20.tolist()
        self.stk_data['list']['v_ma5']  	= mv_ma5.tolist()
        self.stk_data['list']['v_ma10'] 	= mv_ma10.tolist()
        self.stk_data['list']['v_ma20'] 	= mv_ma20.tolist()
#        self.stk_data['list']['turnover'] 	= mturnover .tolist()
        self.stk_data['list']['diff']   	= []   
        self.stk_data['list']['dea']    	= []   
        self.stk_data['list']['macd']   	= []  
        self.stk_data['list']['amount'] 	= [] #成交额
        self.stk_data['list']['mma']    	= []   #分时均价
#rsi
        self.stk_data['list']['rsir']   	= []   
        self.stk_data['list']['rsis']    	= []   
        self.stk_data['list']['risi']   	= []  
#kdj
        self.stk_data['list']['kdjk']   	= []   
        self.stk_data['list']['kdjd']    	= []   
        self.stk_data['list']['kdjj']    	= []   

        self.stk_data['list']['bbi']   	= []   
        self.stk_data['list']['boll_middle']    	= []   
        self.stk_data['list']['boll_upper']   	= []   
        self.stk_data['list']['boll_lower']    	= []   

        self.stk_data['list']['ak']    	= []   
        self.stk_data['list']['ad1']   	= []   
        self.stk_data['list']['aj']    	= []   

        sum_vol = sum(self.stk_data['list']['vol'])
        sum_amt = sum(self.stk_data['list']['amount'])
 
        self.stk_data['list']['mma'].insert(0, float(sum_amt)/(sum_vol))

        self.load_MACD()
        self.load_RSI()
        self.load_KDJ()
        self.load_BBI()
        self.load_BOLL()
        self.load_DZX()

        lll = len(self.stk_data['list']['diff'])
             
#        self.stk_data['pre_close'] = 0.0 #上一个交易日收盘价
        self.stk_data['open'] = self.stk_data['list']['open'][0]       #开盘价
        self.stk_data['high'] = max(self.stk_data['list']['high'])     #最高价
        self.stk_data['low']  = min(self.stk_data['list']['low'])      #最低价
        self.stk_data['close']= self.stk_data['list']['close'][-1]     #收盘价
        self.stk_data['max_vol']  = max(self.stk_data['list']['vol'])  #当日最高成交量

        self.load_SMA()
        #self.stk_data['list']['ma5'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=5).tolist() 
        #self.stk_data['list']['ma10'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=10).tolist() 
        #self.stk_data['list']['ma20'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=20).tolist() 
        #self.stk_data['list']['ma30'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=30).tolist() 
        #self.stk_data['list']['ma60'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=60).tolist() 

        self.datacount = len(self.stk_data['list']['open'])

    def load_realtime_data(self):
        f = open(self.stockid + "realtime", 'r')
        pankou = f.readlines()
        f.close()
        for row in pankou:
            vars = row.split(',')
            tt = vars[0].split('"')
            self.stk_info['name'] = str(tt[1])
            self.stk_data['pre_close'] = float(vars[2]) # pre_close
            self.stk_data['current_price'] = float(vars[3]) # current_price
            stockname = self.stk_info['name']
            real_open = float(vars[1])
            real_preclose = float(vars[2])
            real_high = float(vars[4])
            real_low = float(vars[5])
            self.stk_data['list']['buy_v'] = []
            self.stk_data['list']['buy_p'] = []
            self.stk_data['list']['sell_v'] = []
            self.stk_data['list']['sell_p'] = []
            self.stk_data['list']['buy_v'].append(int(int(vars[10])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['buy_p'].append('%4.3f'%float(vars[11]))
            else:
                self.stk_data['list']['buy_p'].append('%4.2f'%float(vars[11]))
            self.stk_data['list']['buy_v'].append(int(int(vars[12])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['buy_p'].append('%4.3f'%float(vars[13]))
            else:
                self.stk_data['list']['buy_p'].append('%4.2f'%float(vars[13]))
            self.stk_data['list']['buy_v'].append(int(int(vars[14])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['buy_p'].append('%4.3f'%float(vars[15]))
            else:
                self.stk_data['list']['buy_p'].append('%4.2f'%float(vars[15]))
            self.stk_data['list']['buy_v'].append(int(int(vars[16])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['buy_p'].append('%4.3f'%float(vars[17]))
            else:
                self.stk_data['list']['buy_p'].append('%4.2f'%float(vars[17]))
            self.stk_data['list']['buy_v'].append(int(int(vars[18])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['buy_p'].append('%4.3f'%float(vars[19]))
            else:
                self.stk_data['list']['buy_p'].append('%4.2f'%float(vars[19]))
            self.stk_data['list']['sell_v'].append(int(int(vars[20])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['sell_p'].append('%4.3f'%float(vars[21]))
            else:
                self.stk_data['list']['sell_p'].append('%4.2f'%float(vars[21]))
            self.stk_data['list']['sell_v'].append(int(int(vars[22])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['sell_p'].append('%4.3f'%float(vars[23]))
            else:
                self.stk_data['list']['sell_p'].append('%4.2f'%float(vars[23]))
            self.stk_data['list']['sell_v'].append(int(int(vars[24])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['sell_p'].append('%4.3f'%float(vars[25]))
            else:
                self.stk_data['list']['sell_p'].append('%4.2f'%float(vars[25]))
            self.stk_data['list']['sell_v'].append(int(int(vars[26])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['sell_p'].append('%4.3f'%float(vars[27]))
            else:
                self.stk_data['list']['sell_p'].append('%4.2f'%float(vars[27]))
            self.stk_data['list']['sell_v'].append(int(int(vars[28])/100))
            if (QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '1' or
                QtCore.QString(u'%s'%(self.stk_info['code']))[0] == '5'):
                self.stk_data['list']['sell_p'].append('%4.3f'%float(vars[29]))
            else:
                self.stk_data['list']['sell_p'].append('%4.2f'%float(vars[29]))

        if (self.stk_data['current_price'] == 0) and self.datacount > 0:
           self.stk_data['current_price'] = self.stk_data['list']['close'][self.datacount-1]

    def initialize_history(self):
        self.stk_data['list']['time']   	= [] #时间
        self.stk_data['list']['open']   	= [] #开盘价
        self.stk_data['list']['high']   	= [] #最高价
        self.stk_data['list']['close']  	= [] #收盘价
        self.stk_data['list']['low']    	= [] #最低价
        self.stk_data['list']['vol']    	= [] #成交量
        self.stk_data['list']['amount'] 	= [] #成交额
        self.stk_data['list']['mma']    	= []   #分时均价
        self.stk_data['list']['ma5']    	= []   #分时均价
        self.stk_data['list']['ma10']   	= []   #分时均价
        self.stk_data['list']['ma20']   	= []   #分时均价
        self.stk_data['list']['ma30']   	= []   #分时均价
        self.stk_data['list']['ma60']   	= []  
        self.stk_data['list']['mprice_change'] 	= []  
        self.stk_data['list']['mp_change']   	= []  
#macd
        self.stk_data['list']['diff']   	= []   
        self.stk_data['list']['dea']    	= []   
        self.stk_data['list']['macd']   	= []  
#rsi
        self.stk_data['list']['rsir']   	= []   
        self.stk_data['list']['rsis']    	= []   
        self.stk_data['list']['risi']   	= []  
#kdj
        self.stk_data['list']['kdjk']   	= []   
        self.stk_data['list']['kdjd']    	= []   
        self.stk_data['list']['kdjj']    	= []   
#bbi
        self.stk_data['list']['bbi']   	= []   
        self.stk_data['list']['boll_middle']    	= []   
        self.stk_data['list']['boll_upper']   	= []   
        self.stk_data['list']['boll_lower']    	= []   

        self.stk_data['list']['ak']    	= []   
        self.stk_data['list']['ad1']   	= []   
        self.stk_data['list']['aj']    	= []   

        self.stk_data['list']['v_ma5']  	= []  
        self.stk_data['list']['v_ma10'] 	= []  
        self.stk_data['list']['v_ma20'] 	= []  
        self.stk_data['list']['turnover'] 	= []  

    def load_BOLL(self):
        mid = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=20).tolist() 
        stddev = talib.STDDEV(np.array(self.stk_data['list']['close']),timeperiod=20).tolist() 
        upper = []
        lower = []
        for i in range(0,len(self.stk_data['list']['close'])): 
            v222 = (float(mid[i]) + 2.0*float(stddev[i]))
            v111 = (float(mid[i]) - 2.0*float(stddev[i]))
            upper.append(float(v222))
            lower.append(float(v111))
        self.stk_data['list']['boll_middle'] = mid
        self.stk_data['list']['boll_upper'] = upper 
        self.stk_data['list']['boll_lower'] = lower
       
        
    def load_DZX(self):
        var2 = []
        var3 = []
        var4 = []
        var5 = []
        for i in range(0,len(self.stk_data['list']['close'])): 
            v222 = (float(self.stk_data['list']['high'][i]) + float(self.stk_data['list']['low'][i]) + 2.0*float(self.stk_data['list']['close'][i]))/4.0
            var2.append(float(v222))
            var3.append(float(v222))
        
        var3 = talib.EMA(np.array(var2),timeperiod=21).tolist() 
        var4 = talib.STDDEV(np.array(var2),timeperiod=21).tolist() 
        print "var2=", var2
        print "var3=", var3
        print "var4=", var4
        print var3[0]
        aa=np.array(var3)
        print len(aa[np.isnan(aa)])
        if isAllNan(var3) and isAllNan(var4):
            self.stk_data['list']['ak'] = var3  
            self.stk_data['list']['ad1'] = var3  
            self.stk_data['list']['aj'] = var3  
            print "var3 all nan"
            return
        var5 = talib.DIV(talib.SUB(np.array(var2), np.array(var3)), np.array(var4)).tolist()
        for i in range(0,len(var5)): 
            var5[i] = (float(var5[i])*100.0+200)/4.0

        var6 = talib.EMA(np.array(var5),timeperiod=5).tolist() 
        for i in range(0,len(var6)): 
            var6[i] = (float(var6[i])-25)*1.56

        AK = talib.EMA(np.array(var6),timeperiod=2).tolist() 
        print "AK"
        for i in range(0,len(AK)): 
            AK[i] = (float(AK[i]))*1.22
            print AK[i]

        self.stk_data['list']['ak'] = AK 

        if isAllNan(AK):
            return

        AD1 = talib.EMA(np.array(AK),timeperiod=2).tolist() 
        print "AD1"
        for i in range(0,len(AD1)): 
            print AD1[i]
         
        self.stk_data['list']['ad1'] = AD1 

        AJ = talib.EMA(np.array(var6),timeperiod=2).tolist() 
        print "AJ"
        for i in range(0,len(AJ)): 
            AJ[i] = (float(AJ[i]))*1.22

        for i in range(0,len(AJ)): 
            AJ[i] = (float(AJ[i])*3.0-2.0*AD1[i])
            print AJ[i]
        self.stk_data['list']['aj'] = AJ 
       

    def load_BBI(self):
        ma3 = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=3).tolist() 
        ma6 = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=6).tolist() 
        ma12 = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=12).tolist() 
        ma24 = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=24).tolist() 

        self.stk_data['list']['bbi'] = ma24
        for i in range(0,len(self.stk_data['list']['bbi'])): 
            if not pd.isnull(self.stk_data['list']['bbi'][i]):
                self.stk_data['list']['bbi'][i] = (ma3[i] + ma6[i] + ma12[i] + ma24[i])/4.0

    def load_SMA(self):
        self.stk_data['list']['ma5'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=5).tolist() 
        self.stk_data['list']['ma10'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=10).tolist() 
        self.stk_data['list']['ma20'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=20).tolist() 
        self.stk_data['list']['ma30'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=30).tolist() 
        self.stk_data['list']['ma60'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=60).tolist() 

    def load_MACD(self):
        macd,macdsignal,macdhist = talib.MACD(np.array(self.stk_data['list']['close']), fastperiod=12, slowperiod=26, signalperiod=9)
        self.stk_data['list']['diff'] = macd.tolist()
        self.stk_data['list']['dea'] = macdsignal.tolist()
        self.stk_data['list']['macd'] = macdhist.tolist()

    def load_RSI(self):
        rsir = talib.RSI(np.array(self.stk_data['list']['close']),timeperiod=6) 
        self.stk_data['list']['rsir'] = rsir.tolist()
        rsis = talib.RSI(np.array(self.stk_data['list']['close']),timeperiod=12) 
        self.stk_data['list']['rsis'] = rsis.tolist()
        rsii = talib.RSI(np.array(self.stk_data['list']['close']),timeperiod=24) 
        self.stk_data['list']['rsii'] = rsii.tolist()

        
    def load_KDJ(self):
        slowk, slowd = talib.STOCH(np.array(self.stk_data['list']['high']), np.array(self.stk_data['list']['low']), np.array(self.stk_data['list']['close']), fastk_period=9, slowk_period=3, slowk_matype=0, slowd_period=6, slowd_matype=0)
        self.stk_data['list']['kdjk'] = slowk.tolist()
        self.stk_data['list']['kdjd'] = slowd.tolist()
        for i in range(len(self.stk_data['list']['kdjk'])-1,-1,-1):
            if pd.isnull(self.stk_data['list']['kdjk'][i]):
                self.stk_data['list']['kdjj'].insert(0,(self.stk_data['list']['kdjk'][i]))
            else:
                self.stk_data['list']['kdjj'].insert(0,float(self.stk_data['list']['kdjk'][i])*3-2.0*float(self.stk_data['list']['kdjd'][i]))

        #for i in range(len(rsir)-1,-1,-1):
        #    print self.stk_data['list']['time'][i],",",self.stk_data['list']['kdjk'][i],",",self.stk_data['list']['kdjd'][i],",",self.stk_data['list']['kdjj'][i]


    def load_trade_history(self):
        if (self.ktype == 'D'):
            self.load_day_history()
        else:
            if (self.stockid[0] == '1' or self.stockid[0] == '5'):
     	        self.load_ETF_history()
            else:
     	        self.load_minutes_history()

    def load_day_history(self):
        f = open(self.stockid + K_FILE_NAME[self.ktype], 'r')
        data = f.readlines()
        f.close()
        need2update = 0 
        self.datacount = len(data)
        if len(data) == 0: return
        for row in data:
            vars = row.split(',')
            time = vars[0]
            try:
                index = self.stk_data['list']['time'].index(time)
                # if index already exist, then continue
                continue
            except:
#                print "new time stock data to be inserted." 
                need2update = 1
                self.stk_data['list']['time'].insert(0, vars[0])
                self.stk_data['list']['open'].insert(0, float(vars[1]))
                self.stk_data['list']['high'].insert(0, float(vars[2]))
                self.stk_data['list']['close'].insert(0, float(vars[3]))
                self.stk_data['list']['low'].insert(0, float(vars[4]))
                self.stk_data['list']['vol'].insert(0, int(float(vars[5])))
                self.stk_data['list']['amount'].insert(0, int(float(vars[6])))
                 
                sum_vol = sum(self.stk_data['list']['vol'])
                sum_amt = sum(self.stk_data['list']['amount'])
 
                self.stk_data['list']['mma'].insert(0, float(sum_amt)/(sum_vol))

        if (need2update == 0):
            return

        self.load_MACD()
        self.load_RSI()
        self.load_KDJ()
        self.load_BBI()
        self.load_BOLL()
        self.load_DZX()
#        mmin,mmax = talib.MINMAX(np.array(self.stk_data['list']['close']),timeperiod=9) 
        #llv = talib.MIN(np.array(self.stk_data['list']['low']),timeperiod=9).tolist() 
        #hhv = talib.MAX(np.array(self.stk_data['list']['high']),timeperiod=9).tolist() 
        #rsv = []
        #print "MMMMMM"
        #for i in range(len(rsir)-1,-1,-1):
        #    print self.stk_data['list']['time'][i],",",self.stk_data['list']['low'][i],",",llv[i]
        #print "Nnnnnn"
        #for i in range(len(rsir)-1,-1,-1):
        #    print self.stk_data['list']['time'][i],",",self.stk_data['list']['high'][i],",",hhv[i]
        #for i in range(len(rsir)-1,-1,-1):
        #    rsv.insert(0,(float(self.stk_data['list']['close'][i])-float(llv[i]))/(float(hhv[i])-float(llv[i]))*100.0)
        #kdfkkk = talib.SMA(np.array(rsv),timeperiod=3).tolist()
        #kdfddd = talib.SMA(np.array(kdfkkk),timeperiod=3).tolist()
        #kdfjjj = []
        #for i in range(len(rsir)-1,-1,-1):
        #    kdfjjj.insert(0,float(kdfkkk[i])*3.0-2.0*float(kdfddd[i]))
        #print "kkkkkkkkkkk"
        #for i in range(len(rsir)-1,-1,-1):
        #    print self.stk_data['list']['time'][i],",",rsv[i],kdfkkk[i],",",kdfddd[i]

        #fastk, fastd = talib.STOCHF(np.array(self.stk_data['list']['high']), np.array(self.stk_data['list']['low']), np.array(self.stk_data['list']['close']), fastk_period=9, fastd_period=3, fastd_matype=0)

        #print "len rsir = ", len(rsir)
#        for i in range(len(rsir)-1,-1,-1):
#            print self.stk_data['list']['time'][i],",",self.stk_data['list']['kdjk'][i],",",self.stk_data['list']['kdjd'][i],fastk.tolist()[i],",",fastd.tolist()[i]
#        print "rsiiiii"
#        for i in range(len(rsir)-1,-1,-1):
#            print self.stk_data['list']['time'][i],",",self.stk_data['list']['rsir'][i],",",self.stk_data['list']['rsis'][i],self.stk_data['list']['rsii'][i]
#
#        tts = []
#        for i in range(0,len(rsir)):
#            tts.append(i)
#
#        fig = plt.figure()
#        fig.suptitle('stock',fontsize=14,fontweight='bold')
#        ax = fig.add_subplot(1,1,1)
#        ax.set_xlabel("x")
#        ax.set_ylabel("y")
#        ax.plot(tts,self.stk_data['list']['kdjk'])#diff
#        ax.plot(tts,self.stk_data['list']['kdjd'])#diff
#        ax.plot(tts,kdfjjj)#diff
        #ax.plot(tts,self.stk_data['list']['rsir'])#diff
        #ax.plot(tts,self.stk_data['list']['rsis'])#diff
        #ax.plot(tts,self.stk_data['list']['rsii'])#diff
        #plt.legend(('R','S','I'))
#        plt.legend(('K','D','J'))
#        plt.grid(True)
#        plt.show()

        lll = len(self.stk_data['list']['diff'])
             
#        self.stk_data['pre_close'] = 0.0 #上一个交易日收盘价
        self.stk_data['open'] = self.stk_data['list']['open'][0]       #开盘价
        self.stk_data['high'] = max(self.stk_data['list']['high'])     #最高价
        self.stk_data['low']  = min(self.stk_data['list']['low'])      #最低价
        self.stk_data['close']= self.stk_data['list']['close'][-1]     #收盘价
        self.stk_data['max_vol']  = max(self.stk_data['list']['vol'])  #当日最高成交量

        self.load_SMA()
#        self.stk_data['list']['ma5'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=5).tolist() 
#        self.stk_data['list']['ma10'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=10).tolist() 
#        self.stk_data['list']['ma20'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=20).tolist() 
#        self.stk_data['list']['ma30'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=30).tolist() 
#        self.stk_data['list']['ma60'] = talib.SMA(np.array(self.stk_data['list']['close']),timeperiod=60).tolist() 
#        #for row in SMA:
        #    vars = row.split(',')
        #    self.stk_data['list']['ma5'].insert(0, float(vars[1]))
        #    self.stk_data['list']['ma10'].insert(0, float(vars[2]))
        #    self.stk_data['list']['ma20'].insert(0, float(vars[3]))
        #    self.stk_data['list']['ma30'].insert(0, float(vars[4]))
        #    self.stk_data['list']['ma60'].insert(0, float(vars[5]))

        self.datacount = len(self.stk_data['list']['open'])

    def datetime_update_timer(self):
#        self.frameNo += 1
        #print "datetime_update_timer", du.get_now()
        self.update()

    def dumpMACD(self):
        for k in range(0, len(self.stk_data['list']['diff'])):
            diff = float(self.stk_data['list']['diff'][k])
            dea = float(self.stk_data['list']['dea'][k])
            macd = float(self.stk_data['list']['macd'][k])
#            print "diff,dea,macd[",k,"] =", diff, dea, macd 

#    @QtCore.pyqtSlot()
    def setFullScreen(self, on):
        if ((self.windowState() & Qt.WindowFullScreen) == on):
            return;
        if (on):
            self.setWindowState(self.windowState() | Qt.WindowFullScreen)
        else:
            self.setWindowState(self.windowState() & ~Qt.WindowFullScreen)

    def updateRange(self):
        if not (self.datacount > 0):
            return
        tmp = self.stk_data['list']['high'][self.first]
        for i in range(self.first+1, self.last):
            if (self.stk_data['list']['high'][i] > tmp):
                tmp = self.stk_data['list']['high'][i]
        self.price_view_high = tmp
            
        tmp = self.stk_data['list']['low'][self.first]
        for i in range(self.first+1, self.last):
            if self.stk_data['list']['low'][i] < tmp:
                tmp = self.stk_data['list']['low'][i]
        self.price_view_low = tmp
             
        tmp = self.stk_data['list']['vol'][self.first]
        for i in range(self.first+1, self.last):
            if self.stk_data['list']['vol'][i] > tmp:
                tmp = self.stk_data['list']['vol'][i]
        self.view_max_vol = tmp

        tmp = 0.0
        tmp2 = 0.0
        for i in range(self.first, self.last):
            if abs(float(self.stk_data['list']['diff'][i])) > tmp:
                tmp = abs(float(self.stk_data['list']['diff'][i]))
            if abs(float(self.stk_data['list']['dea'][i])) > tmp:
                tmp = abs(float(self.stk_data['list']['dea'][i]))
            if abs(float(self.stk_data['list']['macd'][i])) > tmp2:
                tmp2 = abs(float(self.stk_data['list']['macd'][i]))
        self.diff_dea_range = tmp
        self.macd_range = tmp2

        tmp = 0.0
        tmp2 = 1000.0
        for i in range(self.first, self.last):
            if (float(self.stk_data['list']['kdjk'][i])) > tmp:
                tmp = (float(self.stk_data['list']['kdjk'][i]))
            if (float(self.stk_data['list']['kdjk'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['kdjk'][i]))
            if (float(self.stk_data['list']['kdjd'][i])) > tmp:
                tmp = (float(self.stk_data['list']['kdjd'][i]))
            if (float(self.stk_data['list']['kdjd'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['kdjd'][i]))
            if (float(self.stk_data['list']['kdjj'][i])) > tmp:
                tmp = (float(self.stk_data['list']['kdjj'][i]))
            if (float(self.stk_data['list']['kdjj'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['kdjj'][i]))
        self.KDJ_max = tmp
        self.KDJ_min = tmp2

        tmp = 0.0
        tmp2 = 1000.0
        for i in range(self.first, self.last):
            if (float(self.stk_data['list']['rsir'][i])) > tmp:
                tmp = (float(self.stk_data['list']['rsir'][i]))
            if (float(self.stk_data['list']['rsir'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['rsir'][i]))
            if (float(self.stk_data['list']['rsis'][i])) > tmp:
                tmp = (float(self.stk_data['list']['rsis'][i]))
            if (float(self.stk_data['list']['rsis'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['rsis'][i]))
            if (float(self.stk_data['list']['rsii'][i])) > tmp:
                tmp = (float(self.stk_data['list']['rsii'][i]))
            if (float(self.stk_data['list']['rsii'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['rsii'][i]))
        self.RSI_max = tmp
        self.RSI_min = tmp2
        print "RSI_max = ", self.RSI_max
        print "RSI_min = ", self.RSI_min
        tmp = 0.0
        tmp2 = 1000.0
        for i in range(self.first, self.last):
            if (float(self.stk_data['list']['ak'][i])) > tmp:
                tmp = (float(self.stk_data['list']['ak'][i]))
            if (float(self.stk_data['list']['ak'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['ak'][i]))
            if (float(self.stk_data['list']['ad1'][i])) > tmp:
                tmp = (float(self.stk_data['list']['ad1'][i]))
            if (float(self.stk_data['list']['ad1'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['ad1'][i]))
            if (float(self.stk_data['list']['aj'][i])) > tmp:
                tmp = (float(self.stk_data['list']['aj'][i]))
            if (float(self.stk_data['list']['aj'][i])) < tmp2:
                tmp2 = (float(self.stk_data['list']['aj'][i]))
        self.DZX_max = tmp
        self.DZX_min = tmp2
        print "DZX_max =",self.DZX_max
        print "DZX_min =",self.DZX_min


    def mouseMoveEvent(self, event):
        super(KHistoryWidget, self).mouseMoveEvent(event)
        self.ignore_mouse = 0
        
        self.m_x =  int(event.x())
        self.m_y =  int(event.y())
        self.repaint()

    def eventFilter(self, object, event):
        if isinstance(object, QtGui.QWidget):
            #mouseMoveEvent
            if (event.type() == 5 and self.igm == 1):
                return True
        return QtGui.QWidget.eventFilter(self, object, event)

    def paintEvent(self, event):
        #print "paintEvent width =", self.width(), "height =", self.height()
        self.seq += 1
        report_painter(self)

    def resizeEvent(self, event):
        self.flashw.setGeometry(self.width() - 198, 27, 198, 57)
        self.titlew.setGeometry(self.width() - 198, 2, 198, 20)
        #self.repaint()
 
    def keyPressEvent(self, event):
        key = event.key()
        if key == QtCore.Qt.Key_Left or key == QtCore.Qt.Key_J:
            self.ignore_mouse = 1
            if (self.current_moving_item == -1):
                self.current_moving_item = self.last-self.first-1 
            elif (self.current_moving_item > 0):
                self.current_moving_item -= 1 
            if (self.first > 0) and (self.last > 0) and (self.current_moving_item == 0):
                self.first -= 1 
                self.last -= 1 
            self.updateRange()
            self.update()
        elif key == QtCore.Qt.Key_F8:
            index = KKEYS.index(self.ktype)
            ni = index + 1
            if (ni == len(KKEYS)):
                ni = 0
            while(not os.path.exists(self.stockid + K_FILE_NAME[KKEYS[ni]])):
                ni += 1
                if (ni == len(KKEYS)):
                    ni = 0
            if (ni == index):return
            self.ktype = KKEYS[ni]
             
  #  for i in range(0,len(KKEYS)):
  #      j = i + 1
  #      if (j == len(KKEYS)):
  #          j = 0
  #      print KKEYS[i], K_FILE_NAME[KKEYS[i]], "next = ", KKEYS[j]
  #  print "index of 60",KKEYS.index("60")

  #          if (self.ktype == 'D'):
  #              if os.path.exists(self.stockid + K_FILE_NAME['60']):
  #                  self.ktype = '60'
  #          elif (self.ktype == '60'):
  #              if os.path.exists(self.stockid + K_FILE_NAME['D']):
  #                  self.ktype = 'D'
  #          elif (self.ktype == '30'):
  #              if os.path.exists(self.stockid + K_FILE_NAME['60']):
  #                  self.ktype = '60'
            self.datacount = 0
            self.last = self.first = 0
            self.initialize_history()
            self.load_trade_history()
            self.first = 0
            if (self.datacount > DEFAULT_DAYS):
                self.first = (self.datacount - DEFAULT_DAYS)
            self.last = len(self.stk_data['list']['time'])
            self.current_moving_item = -1 
            self.updateRange()
            self.update()
#        elif key == QtCore.Qt.Key_Q:
#            QtGui.qApp.quit()
        elif key == QtCore.Qt.Key_C: # enable mouseMoveEvent
            self.igm = 0
        elif key == QtCore.Qt.Key_O: # enable mouseMoveEvent
            self.sma = 0 
            self.boll = 1
            self.bbi = 0
           # if (self.sma == 1): 
           #     self.sma = 0 
           # else:
           #     self.sma = 1 
           #     self.bbi = 0
            self.update()
        elif key == QtCore.Qt.Key_B: # enable mouseMoveEvent
            self.sma = 0 
            self.boll = 0
            self.bbi = 1
            #if (self.bbi == 1): 
            #    self.sma = 0 
            #else:
            #    self.sma = 1 
            #    self.bbi = 0
            self.update()
        elif key == QtCore.Qt.Key_A: # enable mouseMoveEvent
            self.sma = 1 
            self.boll = 0
            self.bbi = 0
           # if (self.sma == 1): 
           #     self.sma = 0 
           # else:
           #     self.sma = 1 
           #     self.bbi = 0
            self.update()
        elif key == QtCore.Qt.Key_M: # enable mouseMoveEvent
            self.macdflag = 1
            self.kdjflag = 0
            self.rsiflag = 0
            self.dzxflag = 0
            self.update()
        elif key == QtCore.Qt.Key_D: # enable mouseMoveEvent
            self.macdflag = 0
            self.kdjflag = 0
            self.rsiflag = 0
            self.dzxflag = 1
            self.update()
        elif key == QtCore.Qt.Key_R: # enable mouseMoveEvent
            self.macdflag = 0
            self.kdjflag = 0
            self.dzxflag = 0
            self.rsiflag = 1
            self.update()
        elif key == QtCore.Qt.Key_S: # enable mouseMoveEvent
            self.macdflag = 0
            self.kdjflag = 1
            self.dzxflag = 0
            self.rsiflag = 0
            self.update()
        elif key == QtCore.Qt.Key_Escape:
            self.ignore_mouse = 1
            self.current_moving_item = -1
            self.update()
        elif key == QtCore.Qt.Key_Right or key == QtCore.Qt.Key_L:
            self.ignore_mouse = 1
            if (self.current_moving_item == -1):
                self.current_moving_item = 0 
            elif (self.current_moving_item < self.last-self.first-1):
                self.current_moving_item += 1 
            if (self.last < self.datacount) and (self.current_moving_item == self.last-self.first-1):
                self.first += 1
                self.last += 1
            self.updateRange()
            self.update()
        elif key == QtCore.Qt.Key_Up or key == QtCore.Qt.Key_I:
            self.ignore_mouse = 1
            if ((self.first+68 < self.last)):
                self.first += 1
            self.current_moving_item = -1
            self.updateRange()
            self.update()
        elif key == QtCore.Qt.Key_Down or key == QtCore.Qt.Key_K:
            print "first=",self.first
            print "last=",self.last
            print "datacout=",self.datacount
            self.ignore_mouse = 1
            if (self.first > 0):
                self.first -= 1 
            elif (self.last < self.datacount):
                self.last += 1
            self.current_moving_item = -1
            self.updateRange()
            self.update()
        elif key == QtCore.Qt.Key_PageDown:
            self.ignore_mouse = 1
            self.update()
        elif key == QtCore.Qt.Key_PageUp:
            self.ignore_mouse = 1
            self.update()
        else:
            super(KHistoryWidget, self).keyPressEvent(event)

if __name__ == '__main__':
    app = QtGui.QApplication(sys.argv)
    stockid = sys.argv[1][0:6]
    #dt = KHistoryWidget(sys.argv[1])
    #dt.show()
    widget = MultiPageMainWidget()
#    widget.addPage(KHistoryWidget(sys.argv[1]))
#    widget.addPage(rq.RealTime_Quotes())
    widget.show()
    app.exec_()
