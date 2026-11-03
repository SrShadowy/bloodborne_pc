from PyInstaller.utils.hooks.gi import GiModuleInfo, collect_glib_etc_files, collect_glib_share_files, collect_glib_translations
from PyInstaller.compat import is_win

def hook(hook_api):
    module_info = GiModuleInfo('Gtk', '4.0', hook_api=hook_api)
    if not module_info.available:
        return
    binaries, datas, hiddenimports = module_info.collect_typelib_data()
    datas += collect_glib_share_files('fontconfig')
    datas += collect_glib_share_files('icons')
    datas += collect_glib_share_files('themes')
    datas += collect_glib_translations('gtk40', None)
    if is_win:
        datas += collect_glib_etc_files('fonts')
        datas += collect_glib_etc_files('pango')
        datas += collect_glib_share_files('fonts')
    hook_api.add_datas(datas)
    hook_api.add_binaries(binaries)
    hook_api.add_imports(*hiddenimports)
