component('blebtool', 'executable')

compiler_features('c++11')

use('bleb', 'static-library')

source_files('src/blebtool.cpp')
