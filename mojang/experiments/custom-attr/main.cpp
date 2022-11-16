// ../../llvm-project/build/RelWithDebInfo/bin/clang -Xclang -ast-dump -fsyntax-only main.cpp

struct [[mojang::cerealize]] TestStruct {
    [[mojang::cerealize]] int mTestInt;
};

enum class  [[mojang::cerealize]] TestEnum {
    TestEnum_Value1
};

template<class T>
class [[mojang::cerealize]] TemplateTest : public TemplateTest<T> {
public:
    [[mojang::cerealize]] T mTestType;
};

class Dave : public TemplateTest<int> {
    [[mojang::cerealize]] float mTestFloat;
};