PNAME=lf

export BOOST_ROOT=${DISTDIR}/boost_1_59_0

user_compile(){
    mkdir -p mybuild && cd mybuild
    cmake -DCMAKE_INSTALL_PREFIX=${DISTDIR}/${PNAME} ..
    make VERBOSE=1
}
