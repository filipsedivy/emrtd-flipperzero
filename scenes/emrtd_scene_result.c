/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * The menu over everything the read produced.
 */
#include "../emrtd_i.h"

typedef enum {
    EmrtdSceneResultIndexHolder,
    EmrtdSceneResultIndexDocument,
    EmrtdSceneResultIndexSecurity,
    EmrtdSceneResultIndexFiles,
    EmrtdSceneResultIndexPhoto,
} EmrtdSceneResultIndex;

static void emrtd_scene_result_submenu_callback(void* context, uint32_t index) {
    furi_assert(context);
    Emrtd* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void emrtd_scene_result_on_enter(void* context) {
    furi_assert(context);
    Emrtd* app = context;
    Submenu* submenu = app->submenu;

    submenu_set_header(submenu, "Read result");

    if(app->result.has_mrz) {
        submenu_add_item(
            submenu,
            "Holder",
            EmrtdSceneResultIndexHolder,
            emrtd_scene_result_submenu_callback,
            app);
        submenu_add_item(
            submenu,
            "Document",
            EmrtdSceneResultIndexDocument,
            emrtd_scene_result_submenu_callback,
            app);
    }

    submenu_add_item(
        submenu,
        "Security",
        EmrtdSceneResultIndexSecurity,
        emrtd_scene_result_submenu_callback,
        app);
    submenu_add_item(
        submenu, "Files", EmrtdSceneResultIndexFiles, emrtd_scene_result_submenu_callback, app);

    /* The photo entry is only offered when there is one; an entry that
     * explains its own absence is worse than no entry. */
    if(app->result.has_face) {
        submenu_add_item(
            submenu, "Photo", EmrtdSceneResultIndexPhoto, emrtd_scene_result_submenu_callback, app);
    }

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, EmrtdSceneResult));

    view_dispatcher_switch_to_view(app->view_dispatcher, EmrtdViewSubmenu);
}

bool emrtd_scene_result_on_event(void* context, SceneManagerEvent event) {
    furi_assert(context);
    Emrtd* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event <= EmrtdSceneResultIndexPhoto) {
        scene_manager_set_scene_state(app->scene_manager, EmrtdSceneResult, event.event);
        consumed = true;

        switch(event.event) {
        case EmrtdSceneResultIndexHolder:
            scene_manager_next_scene(app->scene_manager, EmrtdSceneResultHolder);
            break;
        case EmrtdSceneResultIndexDocument:
            scene_manager_next_scene(app->scene_manager, EmrtdSceneResultDocument);
            break;
        case EmrtdSceneResultIndexSecurity:
            scene_manager_next_scene(app->scene_manager, EmrtdSceneResultSecurity);
            break;
        case EmrtdSceneResultIndexFiles:
            scene_manager_next_scene(app->scene_manager, EmrtdSceneResultFileList);
            break;
        case EmrtdSceneResultIndexPhoto:
            scene_manager_next_scene(app->scene_manager, EmrtdSceneResultPhoto);
            break;
        default:
            consumed = false;
            break;
        }
    }

    return consumed;
}

void emrtd_scene_result_on_exit(void* context) {
    furi_assert(context);
    Emrtd* app = context;

    submenu_reset(app->submenu);
}
