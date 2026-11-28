#include <Python.h>

// `python`, as an executable Android lets an app start: named as a library so
// that the package installer puts it in the native library directory, the one
// place an app may exec from since Android 10. Official Android builds of
// CPython come without an executable.
int main(int argc, char** argv)
{
    return Py_BytesMain(argc, argv);
}
