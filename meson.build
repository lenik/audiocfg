project(
    'audiocfg',
    'c',
    version: run_command(
        'sh',
        '-c', '''
        v=$(
            command -v zfr >/dev/null 2>&1 && zfr version 2>/dev/null || true
        )
        if [ -z "$v" ] && [ -f VERSION ]; then
            v=$(head -n1 VERSION | tr -d '\r')
            v=${v#v}
        fi
        if [ -z "$v" ]; then
            v="0.0.0" # FIXED TO 0.0.0, DO NOT MODIFY
        fi
        printf '%s' "$v"
        ''',
        check: false,
    ).stdout().strip(),
    license: 'AGPL-3.0-or-later',
    default_options: [
        'c_std=c99',
        'warning_level=2',
    ],
)

project_author = 'Lenik'
project_email = 'audiocfg@bodz.net'
project_year = 2026

asciidoctor = find_program('asciidoctor', required: true)
fs = import('fs')
c = meson.get_compiler('c')

prefix = get_option('prefix')
bindir = prefix / get_option('bindir')
datadir = prefix / get_option('datadir')
localedir = prefix / get_option('localedir')
mandir = prefix / get_option('mandir')
pkgdocdir = datadir / 'doc' / meson.project_name()

config_h = configuration_data()
config_h.set_quoted('PROJECT_VERSION', meson.project_version())
config_h.set_quoted('PROJECT_AUTHOR', project_author)
config_h.set_quoted('PROJECT_EMAIL', project_email)
config_h.set('PROJECT_YEAR', project_year)

config_h.set_quoted('PREFIX', prefix)
config_h.set_quoted('DATADIR', datadir)
config_h.set_quoted('MANDIR', mandir)
config_h.set_quoted('LOCALEDIR', localedir)

configure_file(
    output: 'config.h',
    configuration: config_h,
)

bas_c_dep = dependency('bas-c', required: true)
libpulse_dep = dependency('libpulse', required: true)

nowarn = c.get_supported_arguments(['-w', '/w'])
bas_args = ['-DLOGGER_NAME=audiocfg', '-DASSETS_NAME=audiocfg']

c_pch = 'src/c_pch.h'
c_args = [nowarn, bas_args, '-DTEXT_DOMAIN=audiocfg']

inc = include_directories('src')

app_deps = [
    bas_c_dep,
    libpulse_dep,
]

subdir('po')

# Core modules linked into the standalone CLI (not a shared library).
core_sources = files(
    'src/session.c',
    'src/parse.c',
    'src/catalog.c',
    'src/select.c',
    'src/profile.c',
)

executable(
    'audiocfg',
    ['src/audiocfg.c'] + core_sources,
    c_args: c_args,
    c_pch: c_pch,
    include_directories: inc,
    dependencies: app_deps,
    install: true,
    install_dir: bindir,
)

bash_files = [
    'audiocfg.bash',
]

foreach file : bash_files
    name = fs.stem(file)
    install_data(
        file,
        install_dir: datadir / 'bash-completion' / 'completions',
        rename: name,
    )
endforeach

install_data(
    [
        'LICENSE',
        'README.md',
        'README-zh.md',
    ],
    install_dir: pkgdocdir,
)

run_target(
    'look',
    command: [
        'bash',
        '-euc', '''
            tmpdir=$(mktemp -d)
            DESTDIR=$tmpdir meson install -C "@BUILD_ROOT@"
            tree "$tmpdir"
            rm -fr "$tmpdir"
        ''',
    ],
)

run_target(
    'install-symlinks',
    command: [
        'bash',
        '-euc', '''
            prefix="@0@"
            bindir="@1@"
            datadir="@2@"
            localedir="@3@"
            mandir="@4@"

            mkdir -p "$bindir" "$datadir/bash-completion/completions" "$localedir" "$mandir/man1"

            sudo ln -sfn "@BUILD_ROOT@/audiocfg" "$bindir/audiocfg"
            sudo ln -sfn "@BUILD_ROOT@/audiocfg.1" "$mandir/man1/audiocfg.1"
            sudo ln -sfn "@SOURCE_ROOT@/audiocfg.bash" "$datadir/bash-completion/completions/audiocfg"

            printf "Symlinks installed under %s\n" "$prefix"
        '''.format(
            get_option('prefix'), #
            bindir,
            datadir,
            localedir,
            mandir,
        ),
    ],
)

run_target(
    'uninstall-symlinks',
    command: [
        'bash',
        '-euc', '''
            prefix="@0@"
            bindir="@1@"
            datadir="@2@"
            localedir="@3@"
            mandir="@4@"

            for p in \
                "$bindir/audiocfg" \
                "$mandir/man1/audiocfg.1" \
                "$datadir/bash-completion/completions/audiocfg"
            do
                if [ -L "$p" ]; then
                    sudo rm -f "$p"
                    printf "Removed %s\n" "$p"
                else
                    printf "Skipped (not a symlink): %s\n" "$p"
                fi
            done
        '''.format(
            get_option('prefix'), #
            bindir,
            datadir,
            localedir,
            mandir,
        ),
    ],
)

if get_option('tests')
    subdir('tests')
endif

man_puffs = [
    'audiocfg',
]
foreach puff : man_puffs
  custom_target(
    puff + '-man',
    input: 'docs' / (puff + '.adoc'),
    output: puff + '.1',
    command: [
      asciidoctor,
      '-b', 'manpage',
      '-a', 'project-version=' + meson.project_version(),
      '-a', 'project-year=@0@'.format(project_year),
      '-a', 'project-author=' + project_author,
      '-a', 'project-email=' + project_email,
      '-o', '@OUTPUT@',
      '@INPUT@',
    ],
    build_by_default: true,
    install: true,
    install_dir: mandir / 'man1',
  )
endforeach

# Translated whole-document mans under docs/<locale>/ (L1 scaffolds).
# Unique build outputs + install script: Meson forbids path segments in
# custom_target output and install_data cannot take a CustomTarget.
man_i18n_langs = [
    'ar',
    'de',
    'es_MX',
    'fr',
    'it',
    'ja',
    'ko',
    'zh_CN',
]
foreach lang : man_i18n_langs
  foreach puff : man_puffs
    man_in = 'docs' / lang / (puff + '.adoc')
    if not fs.exists(man_in)
      continue
    endif
    man_built = custom_target(
      puff + '-man-' + lang,
      input: man_in,
      output: puff + '-' + lang + '.1',
      command: [
        asciidoctor,
        '-b', 'manpage',
        '-a', 'project-version=' + meson.project_version(),
        '-a', 'project-year=@0@'.format(project_year),
        '-a', 'project-author=' + project_author,
        '-a', 'project-email=' + project_email,
        '-o', '@OUTPUT@',
        '@INPUT@',
      ],
      build_by_default: true,
    )
    # Absolute dest under prefix; DESTDIR is prepended (RPM may set mandir absolute).
    meson.add_install_script(
      'sh',
      '-c',
      'install -D -m644 "$1" "${DESTDIR:-}$2"',
      '--',
      man_built,
      mandir / lang / 'man1' / (puff + '.1'),
    )
  endforeach
endforeach
