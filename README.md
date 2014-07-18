QtFtp
=====

this is ftp class,base on QT 5.2+

Ftp ftp=new Ftp(ip,port,username,password);

1.loginSuccess()信号表示登陆成功， 一切操作必须在登陆成功的基础之下
2.一般FTP服务器有一下特性，长时间不使用，会自动超时，断开连接。logout信号会响应的发送出来。


ftp->put()   //上传
ftp->get()   //下载
ftp->list()  //列出目录

note: 这三个函数都是串行执行(执行时间长)。 一个命令没有执行完毕，之后的命令自动无视。

ftp->rawCommand();//执行FTP命令

