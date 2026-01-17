# 网络模拟器（Network Simulator）3 版本

## 目录
1. [项目概述](#项目概述)
2. [编译 ns-3](#编译-ns-3)
3. [运行 ns-3](#运行-ns-3)
4. [获取 ns-3 文档](#获取-ns-3-文档)
5. [使用 ns-3 开发版本](#使用-ns-3-开发版本)
6. [使用 ns-3 该仓库版本](#使用-ns-3-该仓库版本)

注：有关 ns-3 的更多详细信息，可访问官方网站：https://www.nsnam.org


## 项目概述
ns-3 是一个免费的开源项目，旨在构建一款面向仿真研究与教学的离散事件网络模拟器。

这是一个协作型项目。对于我们尚未实现的模型模块，我们期待社区通过开放协作的方式补充完善。

参与 ns-3 项目贡献的方式因人而异，取决于参与者投入的时间、以及计划开发的模型类型。目前项目推荐遵循的贡献流程详见：https://www.nsnam.org/developers/contributing-code/

本 README 文件中的部分内容，摘选自一份更完整的教程。该教程的最新版本可通过以下链接查看：https://www.nsnam.org/documentation/latest/


## 编译 ns-3
ns-3 提供的框架代码与默认模型，会被编译为一组库文件。用户编写的仿真程序，需作为调用这些 ns-3 库文件的简单程序来开发。

若要编译默认库文件及本安装包中包含的示例程序，需使用工具 `waf`。关于 `waf` 的详细使用说明，可参考文件 doc/build.txt。

若需快速上手，可在包含本 README 文件的目录下，依次执行以下命令：
```shell
./waf configure --enable-examples
```
随后执行：
```shell
./waf
```
编译生成的文件会被复制到 build/ 目录中。

当前代码库支持的操作系统平台，已在 [发布说明](RELEASE_NOTES) 文件中列出。

其他操作系统平台的兼容性未做保证。若您能提供补丁以提升代码在其他平台的可移植性，我们将非常欢迎。


## 运行 ns-3
在最新的 Linux 系统中，若已完成 ns-3 编译（需启用示例程序），可通过以下命令轻松运行示例程序。例如：
```shell
./waf --run simple-global-routing
```

该程序会生成一个 `simple-global-routing.tr` 文本跟踪文件，以及一组 `simple-global-routing-xx-xx.pcap` 二进制 pcap 跟踪文件。

这些 pcap 文件可通过 `tcpdump -tt -r filename.pcap` 命令读取。该程序的源代码位于 examples/routing 目录下。


## 获取 ns-3 文档
若您已通过运行 3）中提到的 simple-point-to-point 示例，确认 ns-3 编译成功，接下来可开始阅读 ns-3 相关文档。

所有文档均可通过 ns-3 官方网站获取：https://www.nsnam.org/documentation/，具体包括：
- 教程（tutorial）
- 参考手册（reference manual）
- ns-3 模型库中的各类模型（models in the ns-3 model library）
- 用户贡献技巧的维基页面：https://www.nsnam.org/wiki/
- 使用 doxygen 生成的 API 文档（这是一份参考手册，不太适合作为入门文本）：https://www.nsnam.org/doxygen/index.html


## 使用 ns-3 开发版本
若需下载并使用 ns-3 开发版本，需借助工具 `git`。

手册中包含一份简易的 `git` 使用指南，但如果您不熟悉 `git`，建议先阅读互联网上的 `git` 教程。

若已成功安装 `git`，可通过以下命令获取开发版本的代码副本：
```shell
git clone https://gitlab.com/nsnam/ns-3-dev.git
```

不过，我们建议初学者遵循 Gitlab 上的指南操作，具体包括：创建 Gitlab 账号、在新账号下复刻（fork）ns-3-dev 项目、然后克隆复刻后的仓库。更多详细信息可参考 [官方手册](https://www.nsnam.org/docs/manual/html/working-with-git.html)。

## 使用 ns-3 该仓库版本
## 编译步骤
### 1. 编译 SDN 底层库（ofsoftswitch13）：
```bash
# 进入 ofsoftswitch13 源码目录
cd /home/your-home/ns-allinone-3.34/ns-3.34/contrib/ofswitch13/lib/ofsoftswitch13

# 生成 configure 脚本
./boot.sh

# 配置并开启 ns3-lib 支持
./configure --enable-ns3-lib

# 并行编译（8 线程）
make -j8
```
编译完成之后会在lib/ofsoftswitch13/udatapath文件夹下生成一个重要文件 libns3ofswitch13.a，ns3编译时就是找是否存在这个文件，有的话才会开启openflow13的编译。
### 2. 编译整个项目：
```bash
# 回到 NS-3 根目录
cd /home/your-home/ns-allinone-3.34/ns-3.34

# 配置构建
./waf configure

# 开始编译
./waf
```

### 3. 验证 OpenFlow 1.3 模块是否启用：

### 4.好的参考
https://maizhude.github.io/
http://www.lrc.ic.unicamp.br/ofswitch13/
https://www.nsnam.org/
https://github.com/CPqD/ofsoftswitch13/wiki
