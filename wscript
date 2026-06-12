#
# This file is the default set of rules to compile a Pebble application.
#
# Feel free to customize this to your needs.
#
import os.path

top = '.'
out = 'build'


def options(ctx):
    ctx.load('pebble_sdk')


def configure(ctx):
    """
    This method is used to configure your build. ctx.load(`pebble_sdk`) automatically configures
    a build for each valid platform in `targetPlatforms`. Platform-specific configuration: add your
    change after calling ctx.load('pebble_sdk') and make sure to set the correct environment first.
    Universal configuration: add your change prior to calling ctx.load('pebble_sdk').
    """
    ctx.load('pebble_sdk')


def build(ctx):
    ctx.load('pebble_sdk')

    build_worker = os.path.exists('worker_src')
    binaries = []

    cached_env = ctx.env
    for platform in ctx.env.TARGET_PLATFORMS:
        ctx.env = ctx.all_envs[platform]
        ctx.set_group(ctx.env.PLATFORM_NAME)
        app_elf = '{}/pebble-app.elf'.format(ctx.env.BUILD_DIR)
        defines = ['SCREENSHOT_FIXTURES'] if os.environ.get('SCREENSHOT_FIXTURES') else []
        ctx.pbl_build(source=ctx.path.ant_glob('src/c/**/*.c'), target=app_elf, bin_type='app', defines=defines)

        if build_worker:
            worker_elf = '{}/pebble-worker.elf'.format(ctx.env.BUILD_DIR)
            binaries.append({'platform': platform, 'app_elf': app_elf, 'worker_elf': worker_elf})
            ctx.pbl_build(source=ctx.path.ant_glob('worker_src/c/**/*.c'),
                          target=worker_elf,
                          bin_type='worker')
        else:
            binaries.append({'platform': platform, 'app_elf': app_elf})
    ctx.env = cached_env

    ctx.set_group('bundle')
    ctx.pbl_bundle(binaries=binaries,
                   js=ctx.path.ant_glob(['src/pkjs/**/*.js',
                                         'src/pkjs/**/*.json',
                                         'src/common/**/*.js']),
                   js_entry_file='src/pkjs/index.js')

    # The Pebble SDK bundler drops unknown appinfo.json fields, including
    # `companionApp`. The Core/Pebble app reads `companionApp.android.apps[].pkg`
    # from the bundled appinfo.json to route watch->phone AppMessages via
    # PebbleKit Android 2 — without it, it falls back to (unsupported) classic
    # PebbleKit and nothing is delivered. So re-inject it into the .pbw here.
    ctx.add_post_fun(inject_companion_app)


def inject_companion_app(ctx):
    import json, zipfile, shutil, glob, os
    pkg_path = ctx.path.find_node('package.json').abspath()
    companion = json.load(open(pkg_path)).get('pebble', {}).get('companionApp')
    if not companion:
        return
    for pbw in glob.glob(os.path.join(out, '*.pbw')):
        zin = zipfile.ZipFile(pbw, 'r')
        if 'appinfo.json' not in zin.namelist():
            zin.close()
            continue
        info = json.loads(zin.read('appinfo.json'))
        if info.get('companionApp') == companion:
            zin.close()
            continue
        info['companionApp'] = companion
        tmp = pbw + '.tmp'
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as zout:
            for item in zin.infolist():
                data = zin.read(item.filename)
                if item.filename == 'appinfo.json':
                    data = json.dumps(info).encode()
                zout.writestr(item, data)
        zin.close()
        shutil.move(tmp, pbw)
        from waflib import Logs
        Logs.pprint('CYAN', 'Injected companionApp into %s' % pbw)
