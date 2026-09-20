/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * Which files the reader will attempt.
 *
 * Every elementary file of ICAO Doc 9303 part 10 is listed, including the
 * ones that cannot be switched, because the question this screen is opened
 * with is usually "why did it not read DG3" and the answer has to be on it.
 */
#include "../emrtd_i.h"

/*
 * The list keeps the label pointer it is given rather than a copy, so the
 * labels have to outlive the screen. "DG1 MRZ" says more than either half of
 * it alone, so they are composed once, here.
 */
static char emrtd_scene_data_groups_labels[EmrtdFileCount][24];

/** The item showing each file, valid only while the screen is up. */
static VariableItem* emrtd_scene_data_groups_items[EmrtdFileCount];

static bool emrtd_scene_data_groups_selectable(const EmrtdFileInfo* info) {
    return info != NULL && info->dg_number >= 0 && !info->eac_protected;
}

static void emrtd_scene_data_groups_apply(Emrtd* app, size_t id, bool selected) {
    if(selected) {
        app->config.files |= EMRTD_FILE_BIT(id);
    } else {
        app->config.files &= ~EMRTD_FILE_BIT(id);
    }

    VariableItem* item = emrtd_scene_data_groups_items[id];
    if(item != NULL) {
        variable_item_set_current_value_index(item, selected ? 1 : 0);
        variable_item_set_current_value_text(item, selected ? "Read" : "Skip");
    }
}

static void emrtd_scene_data_groups_changed(VariableItem* item) {
    Emrtd* app = variable_item_get_context(item);
    const uint8_t position = variable_item_list_get_selected_item_index(app->variable_item_list);

    /* One item per file, added in file order, so the position is the id. */
    if(position >= EmrtdFileCount) {
        return;
    }

    emrtd_scene_data_groups_apply(app, position, variable_item_get_current_value_index(item) != 0);
}

static void emrtd_scene_data_groups_enter_callback(void* context, uint32_t index) {
    furi_assert(context);
    Emrtd* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void emrtd_scene_data_groups_on_enter(void* context) {
    furi_assert(context);
    Emrtd* app = context;
    VariableItemList* list = app->variable_item_list;

    memset(emrtd_scene_data_groups_items, 0, sizeof(emrtd_scene_data_groups_items));

    for(size_t id = 0; id < EmrtdFileCount; id++) {
        const EmrtdFileInfo* info = emrtd_file_info((EmrtdFileId)id);
        char* label = emrtd_scene_data_groups_labels[id];
        const size_t label_size = sizeof(emrtd_scene_data_groups_labels[0]);

        if(info == NULL) {
            snprintf(label, label_size, "File %u", (unsigned)id);
            emrtd_scene_data_groups_items[id] = variable_item_list_add(list, label, 1, NULL, app);
            continue;
        }

        /* "EF.DG1" reads better without the prefix the standard gives it. */
        const char* short_name = info->name;
        if(strncmp(short_name, "EF.", 3) == 0) {
            short_name += 3;
        }
        snprintf(label, label_size, "%s %s", short_name, info->label);

        VariableItem* item;
        if(info->dg_number < 0) {
            /* EF.COM lists what is on the chip and EF.SOD carries the hashes
             * every other file is checked against; neither is optional. */
            item = variable_item_list_add(list, label, 1, NULL, app);
            variable_item_set_current_value_text(item, "Always");
        } else if(info->eac_protected) {
            /* Fingerprints and iris images need a certificate issued by the
             * state that made the document, which this reader has no way of
             * holding. ICAO 9303-11, 4.6. */
            item = variable_item_list_add(list, label, 1, NULL, app);
            variable_item_set_current_value_text(item, "EAC");
        } else {
            const bool selected = (app->config.files & EMRTD_FILE_BIT(id)) != 0;
            item = variable_item_list_add(list, label, 2, emrtd_scene_data_groups_changed, app);
            variable_item_set_current_value_index(item, selected ? 1 : 0);
            variable_item_set_current_value_text(item, selected ? "Read" : "Skip");
        }
        emrtd_scene_data_groups_items[id] = item;
    }

    /* The list refuses a null callback, and it is shared with the options
     * screen, so it is always given one. Here it makes the centre key do what
     * left and right do, which is what a list of switches invites. */
    variable_item_list_set_enter_callback(list, emrtd_scene_data_groups_enter_callback, app);
    variable_item_list_set_selected_item(
        list, (uint8_t)scene_manager_get_scene_state(app->scene_manager, EmrtdSceneDataGroups));

    view_dispatcher_switch_to_view(app->view_dispatcher, EmrtdViewVariableItemList);
}

bool emrtd_scene_data_groups_on_event(void* context, SceneManagerEvent event) {
    furi_assert(context);
    Emrtd* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event < EmrtdFileCount) {
        const size_t id = event.event;
        const EmrtdFileInfo* info = emrtd_file_info((EmrtdFileId)id);

        if(emrtd_scene_data_groups_selectable(info)) {
            emrtd_scene_data_groups_apply(app, id, (app->config.files & EMRTD_FILE_BIT(id)) == 0);
        }
        consumed = true;
    }

    return consumed;
}

void emrtd_scene_data_groups_on_exit(void* context) {
    furi_assert(context);
    Emrtd* app = context;

    scene_manager_set_scene_state(
        app->scene_manager,
        EmrtdSceneDataGroups,
        variable_item_list_get_selected_item_index(app->variable_item_list));

    variable_item_list_reset(app->variable_item_list);
    memset(emrtd_scene_data_groups_items, 0, sizeof(emrtd_scene_data_groups_items));

    emrtd_settings_save(app);
}
