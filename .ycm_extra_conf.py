
def FlagsForFile( filename, **kwargs ): return { 'flags' : [
    '-x', 'c++', '-std=c++11', '-Wall',
    '-DQT_NO_DEBUG_OUTPUT',
    '-DQT_NO_DEBUG',
    '-DQT_WIDGETS_LIB',
    '-DQT_GUI_LIB',
    '-DQT_CORE_LIB',
    '-isystem/usr/include/x86_64-linux-gnu/qt5',
    '-isystem/usr/include/x86_64-linux-gnu/qt5/QtWidgets',
    '-isystem/usr/include/x86_64-linux-gnu/qt5/QtGui',
    '-isystem/usr/include/x86_64-linux-gnu/qt5/QtCore',
    '-isystem/home/m5/sw/utility/Current/include',
    ]}

