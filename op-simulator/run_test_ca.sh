#!/bin/bash
EXECUTABLE="test_template"
ulimit -n 65536
CANN_DIR=$ASCEND_HOME_PATH
echo $EXECUTABLE
echo $CANN_DIR
export CANN_INC="${CANN_DIR}/aarch64-linux/include/"
export ASCEND_TOOLKIT_HOME=$ASCEND_HOME_PATH
g++ -std=c++17 -g -I.. -o ${EXECUTABLE} ${EXECUTABLE}.cpp -I"$CANN_INC" \
   -I"${CANN_DIR}/aarch64-linux/include" \
   -I"${CANN_DIR}/aarch64-linux/include/experiment/runtime/runtime" \
   -I"${CANN_DIR}/aarch64-linux/include/experiment/msprof" \
   -L${CANN_DIR}/aarch64-linux/simulator/Ascend910B1/lib \
   -L"${CANN_DIR}/aarch64-linux/lib64" \
   -L"${CANN_DIR}/aarch64-linux/lib64/plugin/opskernel" \
   -L"${CANN_DIR}/acllib/lib64/stub"\
   -lruntime_camodel -lnpu_drv -lnpu_drv_camodel \
   "${CANN_DIR}/tools/simulator/Ascend910B1/lib/libffts_model.so" -lstars -lffts -lpem_davinci -lmodel_top -lmcu_wrapper -lmcu_loop \
   -L${CANN_DIR}/lib64/stub -lascendcl

export LD_LIBRARY_PATH="${CANN_DIR}/aarch64-linux/simulator/Ascend910B1/lib/":"${CANN_DIR}/tools/simulator/Ascend910B1/lib/":$LD_LIBRARY_PATH
msprof op simulator --soc-version=Ascend910B1 ./${EXECUTABLE}
