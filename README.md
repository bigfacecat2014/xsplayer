# xsplayer
xsplayer是一个采用C++开发的用于虚拟现实应用场景的H.265流媒体播放器。
由于IE的市场份额衰败太快，WebRTC技术发展势头太猛，原生应用的开发商越来越少，作者决定将此播放器捐献给开源社区，供喜欢Windows多媒体技术栈的开发者学习之用。

## 功能简介
- 1.xsplayer是一个基于Windows平台开发的RTSP媒体流播放器，仅支持H.265视频流，最大可同时播放16路HEVC实时视频流。
- 2.xsengine.dll被注册为IE可加载的ActiveX控件后，即可被IE中的Web前端程序调用。
- 3.xsengine.dll可播放最大16路采用RTSP协议传输的H.265实时网络串流。
- 4.xsengine.dll适用于Win7/Win8/Win8.1/Win10等桌面操作系统。
- 5.此项目可配合作者提供的live555项目进行测试体验。

## live555流媒体服务器用法
- 1.git clone https://github.com/bigfacecat2014/live555.git
- 2.打开Live555.sln，按F5键运行。
- 3.将媒体文件（比如:test.mkv）存放在live555.exe的运行目录下。
- 4.修改xsplayer项目代码的RTSP媒体源地址，让其指向运行的live555.exe的点播地址（比如：rtsp://YOUR_HOST_IP/test.mkv）。
- 5.运行xsplayer.exe。

## 构建说明
- 可用VS2019打开.sln文件，直接构建肯定会报错，因为缺少ffmpeg库和头文件。
- ffmpeg库需要你自己去构建一个，将构建的动态库放对应的输出目录下，并设置正确的ffmpeg包含文件目录。
- 如果构建时遇到问题，可以尝试发邮件咨询作者：20775651@qq.com，作者看到后有条件会帮你解答。

## 免责声明
这是一个学习多媒体技术的实践项目，其完成度没有达到商业应用要求，仅供使用者学习研究之用，如果使用者将此项目的代码用于任何其它目的，作者将不承担任何责任。

## 联系方式
 - 个人网站：<http://codemi.net>   
 - 微信公众号：
 <img src=https://github.com/bigfacecat2014/xsplayer/blob/main/adm_wx_gzh.jpg width=50% />
 
