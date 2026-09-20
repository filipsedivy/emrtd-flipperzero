/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Filip Sedivy
 *
 * The read itself.
 *
 * The worker runs on the NFC stack's thread and reports through a callback
 * that does two things and no more: it copies the progress into the
 * application object and posts a custom event. Everything the user sees is
 * drawn from here, on the GUI thread, when that event arrives. The poller is
 * never stopped from inside its own callback - that deadlocks - so it is
 * stopped in on_exit, after the scene has been left.
 */
#include "../emrtd_i.h"

#include <dolphin/dolphin.h>

#define TAG "EmrtdSceneRead"

static bool emrtd_scene_read_worker_callback(const EmrtdWorkerProgress* progress, void* context) {
    furi_assert(progress);
    furi_assert(context);
    Emrtd* app = context;

    const EmrtdWorkerStage previous = app->progress.stage;
    app->progress = *progress;

    uint32_t event = EmrtdCustomEventWorkerProgress;
    if(progress->stage == EmrtdWorkerStageDone) {
        event = EmrtdCustomEventWorkerSuccess;
    } else if(progress->stage == EmrtdWorkerStageError) {
        event = EmrtdCustomEventWorkerError;
    } else if(
        previous <= EmrtdWorkerStageAuthenticating &&
        progress->stage > EmrtdWorkerStageAuthenticating &&
        progress->stage < EmrtdWorkerStageDone) {
        /*
         * The first stage past authentication, whether or not the worker
         * stopped on Authenticating long enough to report it. This is the
         * first moment the access method can be named on screen; failure is
         * already handled above, so reaching here means a driver succeeded.
         */
        event = EmrtdCustomEventWorkerAuthenticated;
    }

    view_dispatcher_send_custom_event(app->view_dispatcher, event);

    /* Stopping is asked for through emrtd_worker_stop() from the GUI thread;
     * returning false here would abort the read on every redraw. */
    return true;
}

static void emrtd_scene_read_view_callback(void* context) {
    furi_assert(context);
    Emrtd* app = context;

    view_dispatcher_send_custom_event(app->view_dispatcher, EmrtdCustomEventViewExit);
}

void emrtd_scene_read_on_enter(void* context) {
    furi_assert(context);
    Emrtd* app = context;

    dolphin_deed(DolphinDeedNfcRead);

    /* Nothing of a previous read may show through: the view keeps its model
     * between visits, and a stale stage line on a retry would be a lie. */
    memset(&app->result, 0, sizeof(app->result));
    memset(&app->progress, 0, sizeof(app->progress));
    app->progress.stage = EmrtdWorkerStageWaitingForCard;

    emrtd_read_view_set_callback(app->read_view, emrtd_scene_read_view_callback, app);
    emrtd_read_view_set_access(app->read_view, NULL);
    emrtd_read_view_set_progress(app->read_view, &app->progress);

    /*
     * The radio is taken for the length of the read and given back in
     * on_exit, so that browsing saved reads does not hold the NFC hardware.
     */
    app->nfc = nfc_alloc();
    app->worker = emrtd_worker_alloc();
    emrtd_worker_set_config(app->worker, &app->config);
    emrtd_worker_set_callback(app->worker, emrtd_scene_read_worker_callback, app);
    emrtd_worker_start(app->worker, app->nfc);

    emrtd_blink_start(app);

    view_dispatcher_switch_to_view(app->view_dispatcher, EmrtdViewRead);
}

bool emrtd_scene_read_on_event(void* context, SceneManagerEvent event) {
    furi_assert(context);
    Emrtd* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case EmrtdCustomEventWorkerProgress:
            emrtd_read_view_set_progress(app->read_view, &app->progress);
            consumed = true;
            break;

        case EmrtdCustomEventWorkerAuthenticated: {
            emrtd_read_view_set_progress(app->read_view, &app->progress);

            /*
             * The access outcome is complete before this event was posted and
             * the event queue orders the two, so reading it here is safe even
             * though the rest of the result is not final until the worker
             * stops. Only the outcome is taken.
             */
            const EmrtdReadResult* result = emrtd_worker_result(app->worker);
            if(result != NULL) {
                app->result.access = result->access;
                app->result.authenticated = true;
                if(app->result.access.summary[0] != '\0') {
                    emrtd_read_view_set_access(app->read_view, app->result.access.summary);
                }
            }
            consumed = true;
            break;
        }

        case EmrtdCustomEventWorkerSuccess: {
            const EmrtdReadResult* result = emrtd_worker_result(app->worker);
            if(result != NULL) {
                /* Copied before the scene changes: next_scene runs on_exit,
                 * and on_exit frees the worker this points into. */
                app->result = *result;
            }
            dolphin_deed(DolphinDeedNfcReadSuccess);
            scene_manager_next_scene(app->scene_manager, EmrtdSceneReadSuccess);
            consumed = true;
            break;
        }

        case EmrtdCustomEventWorkerError: {
            const EmrtdReadResult* result = emrtd_worker_result(app->worker);
            if(result != NULL) {
                app->result = *result;
            } else {
                /* The worker reported a failure it cannot describe. Saying so
                 * is still better than an error screen with no error on it. */
                app->result.error = EmrtdErrorInternal;
            }

            if(app->result.error == EmrtdErrorCancelled) {
                /* The user left; there is nothing to report. */
                scene_manager_previous_scene(app->scene_manager);
            } else {
                scene_manager_next_scene(app->scene_manager, EmrtdSceneReadError);
            }
            consumed = true;
            break;
        }

        case EmrtdCustomEventViewExit:
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
            break;

        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeBack) {
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }

    return consumed;
}

void emrtd_scene_read_on_exit(void* context) {
    furi_assert(context);
    Emrtd* app = context;

    emrtd_blink_stop(app);

    /*
     * Order matters. The worker has to be stopped before it is freed, and the
     * poller has to be idle before nfc_free(), which checks that and aborts
     * the application if it is not.
     */
    if(app->worker != NULL) {
        emrtd_worker_stop(app->worker);
        emrtd_worker_free(app->worker);
        app->worker = NULL;
    }
    if(app->nfc != NULL) {
        nfc_free(app->nfc);
        app->nfc = NULL;
    }
}
