mpy fb增强代码

2023.3.4修改
1、增加单色驱动的字节格式，单色，gs2，gs4都支持字节顺序和水平垂直排列
2、增加了总体的x,y镜像，和数据移动方式的操作，使得不借助屏幕硬件就能实现任意方向的旋转
3、彩色显示增加了RGB888/RGB8888
4、增加了一个比较新型的仿电子纸屏幕的驱动
5、从官方版本引入了椭圆、多边形和新版的bilt。
6、增加了ToGBK方法，可以获得utf8字符串的gb2312/gbk字符串。
b=lcd.ToGBK("Micro python中文甒甒")
输入参数是utf8的字符串
输出是gb或者gbk的字符串，支持中英文混合串
7、增加了曲线函数curve，接收一个数组绘制曲线
lcd.curve(buf,mode,col,x0,y0,x_scale,y_scale)
buf输入的数组，支持bytearray，array的B/b/H/h共计5种格式
mode显示模式，0-点，1-线，2-从x0到目标的线，默认值为0
x0,y0绘制的0点，默认值为高宽值的一半
x_scale，每一个和上一个点的横向移动距离，默认1
y_scale，数据在y轴上的高度，默认是高度的一半
8、增加了gs2/gs4/gs8的bmp读写操作。
9、修正了单色bmp文件显示非整数宽度的错误。
10、调整0x00的字体为5*7点阵，占用6*8的显示空间

编译固件直接把两个文件复制到extmod目录即可

font_asc.h是英文字库的文件，包括4种尺寸的合计12个字体数组。

modframebuf.c是修改后的代码，相比于原本的版本增加了以下几个函数：

修改了text显示函数，画字符功能增加了字体和放大等功能的处理，处理串里面的utf8字符，判定是asc字符还是其他字符，asc字符调用getasc获取asc字模，其他字符调用gethzk获取字模。
配套功能font_load，告知fb组件需要使用的字库文件，不声明文件时，英文仍然可以正常显示，中文会被跳过。font_free是解除字库文件。
font_set用于设置字体大小、旋转、放大和反白。

增加了show_bmp，用于处理rgb格式或者二值格式的bmp文件显示。
增加了save_bmp，用于处理rgb格式或者二值格式的fb内容保存成bmp文件。

modframebuf.c 和 font_asc.h复制到extmod目录

然后直接按照原本的编译方式就可以工作了

测试过esp32，esp8266，stm32f407都是可以工作的

增加的英文字库差不多有100k，程序代码增加约20k，编译不能通过的时候注意调整程序空间的大小，比如512kflash的芯片，默认配置都是不够用的
