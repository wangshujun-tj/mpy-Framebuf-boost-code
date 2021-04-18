mpy fb增强代码

font_asc.h是英文字库的文件，包括4种尺寸的合计12个字体数组。

modframebuf.c是修改后的代码，相比于原本的版本增加了以下几个函数：

修改了text显示函数，画字符功能增加了字体和放大等功能的处理，处理串里面的utf8字符，判定是asc字符还是其他字符，asc字符调用getasc获取asc字模，其他字符调用gethzk获取字模。
配套功能font_load，告知fb组件需要使用的字库文件，不声明文件时，英文仍然可以正常显示，中文会被跳过。font_free是解除字库文件。
font_set用于设置字体大小、旋转、放大和反白。

增加了show_bmp，用于处理rgb格式或者二值格式的bmp文件显示。
增加了save_bmp，用于处理rgb格式或者二值格式的fb内容保存成bmp文件。

以上功能在esp32环境下，mpy1.13版本上测试通过，编译时需要对
py/stream.c 文件的大约430行，去掉   STATIC 

mp_obj_t stream_seek(size_t n_args, const mp_obj_t *args) 

并在
py/stream.h 文件中114行添加
mp_obj_t stream_seek(size_t n_args, const mp_obj_t *args);

modframebuf.c 和 font_asc.h复制到extmod目录

然后直接按照原本的编译方式就可以工作了

测试过esp32，esp8266，stm32f407都是可以工作的
