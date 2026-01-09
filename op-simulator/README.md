# david_kernel_test
## 1）quick start
1、如果是需要算子编译和测试，先修改src下的base_test.cpp以及相应的kernel头文件，然后执行如下命令：
```shell
bash compile_and_run.sh
```
2、如果是已经通过其他工程得到了算子的o文件，只需要运行该核函数，则先修改bash中的kernel路径以及cann路径，然后执行如下命令：
```shell
bash run_test_ca.sh
```
## 2）详细执行
可以支持自定义SIMD算子的编译、功能仿真以及性能仿真测试
该bash中包含了如下所有需要的命令

1. 安装cann包，msprof工具在cann包内
```shell
bash CANN-opp-*.run --full --install-path=/xxx/cann
bash CANN-runtime-*.run --full --install-path=/xxx/cann
bash CANN-compiler-*.run --full --pylocal --install-path=/xxx/cann
bash CANN-toolkit-*.run --full --pylocal --install-path=/xxx/cann
bash Ascend-mindstudio-toolkit_8.0.0_linux-x86_64.run --full --install-path=/xxx/cann
```
2. 项目的根目录下，设置cann环境变量
```shell
bash # 进入到linux的bash解释器
export ASCEND_HOME_PATH=/usr/local/Ascend/ascend-toolkit/latest
source ${ASCEND_HOME_PATH}/bin/setenv.bash # 设置cann以及simulator的环境变量 按需修环境变量的路径
echo $LD_LIBRARY_PATH # 确认环境变量是否配置成功
```
3. 配置工程需要的环境变量
```shell
bash # 进入到linux的bash解释器
export KERNEL_NAME=DemoTest
```
3. 进入到项目根目录，编译.o算子
```shell
bash scripts/run.sh
```
编译后算子目标文件在 ./op/my_kernel.o
4. 进入到项目根目录，进行仿真，执行
```shell
./run_test_ca.sh
```
