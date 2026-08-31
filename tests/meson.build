check_dep = dependency('check', required: get_option('tests'))

test_inc = include_directories('../src', '..')

test_core = files(
    '../src/session.c',
    '../src/parse.c',
    '../src/catalog.c',
    '../src/select.c',
    '../src/profile.c',
)

test_exe = executable(
    'audiocfg_test',
    ['audiocfg_test.c'] + test_core,
    c_args: c_args,
    c_pch: '../' + c_pch,
    include_directories: test_inc,
    dependencies: [app_deps, check_dep],
    install: false,
)

test('audiocfg', test_exe)
