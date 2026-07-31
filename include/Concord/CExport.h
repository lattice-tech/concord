#ifndef CONCORD_CEXPORT_H
#define CONCORD_CEXPORT_H

/**
 * DLL export/import macros.
 *
 * Every engine module compiles into its own DLL. CMake defines
 * `<MODULE>_EXPORTS` while compiling that module's own sources, which
 * selects the export macro below; every other translation unit (the
 * application, or a different module) only ever sees the import macro.
 */
#if defined(CONCORD_STATIC_LINK)
    // Offline tools (e.g. concord-cook) compile engine sources into a static
    // library or directly into the executable; no DLL boundary exists there.
    #define CONCORD_DLL_EXPORT
    #define CONCORD_DLL_IMPORT
#elif defined(_WIN32)
    #define CONCORD_DLL_EXPORT __declspec(dllexport)
    #define CONCORD_DLL_IMPORT __declspec(dllimport)
#else
    #define CONCORD_DLL_EXPORT __attribute__((visibility("default")))
    #define CONCORD_DLL_IMPORT
#endif

/** Export macro for CEngine.dll. */
#if defined(CENGINE_EXPORTS)
    #define CENGINE_API CONCORD_DLL_EXPORT
#else
    #define CENGINE_API CONCORD_DLL_IMPORT
#endif

/** Export macro for CAudio.dll. */
#if defined(CAUDIO_EXPORTS)
    #define CAUDIO_API CONCORD_DLL_EXPORT
#else
    #define CAUDIO_API CONCORD_DLL_IMPORT
#endif

/** Export macro for CGUI.dll. */
#if defined(CGUI_EXPORTS)
    #define CGUI_API CONCORD_DLL_EXPORT
#else
    #define CGUI_API CONCORD_DLL_IMPORT
#endif

/** Export macro for CTime.dll. */
#if defined(CTIME_EXPORTS)
    #define CTIME_API CONCORD_DLL_EXPORT
#else
    #define CTIME_API CONCORD_DLL_IMPORT
#endif

/** Export macro for CSystem.dll. */
#if defined(CSYSTEM_EXPORTS)
    #define CSYSTEM_API CONCORD_DLL_EXPORT
#else
    #define CSYSTEM_API CONCORD_DLL_IMPORT
#endif

#endif // CONCORD_CEXPORT_H
