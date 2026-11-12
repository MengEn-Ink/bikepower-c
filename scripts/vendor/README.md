# 构建期第三方 JavaScript 压缩器

- rJSmin 1.2.5，来源 https://pypi.org/project/rjsmin/1.2.5/ 与 https://github.com/ndparker/rjsmin 。
- `rjsmin.py` 保持上游完整文件，Apache-2.0 许可证见 `rjsmin.LICENSE`。
- 仅固件构建、测试和离线演示生成时执行，显式选用纯 Python 函数，不依赖可选 C 扩展，不下载网络资源；上游模块初始化仍会探测可选扩展。不把压缩器代码放进设备固件。
- 不改变量名、不改控制流，仅删除注释/冗余空白，保留 `/*!...*/`。它不代替 JS 语法检查；嵌套模板等新增语法必须先加等价执行回归。
- main/CMakeLists 与开发源码快照均登记此输入，更新必须保持许可证与比较原始字节身份。
