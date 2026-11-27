// SPDX-FileCopyrightText: Copyright 2026 Eglantine Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <QPixmap>
#include <QString>

#include "common/common_types.h"

struct GameDetailsInfo {
    QString path;
    u64 program_id = 0;
    QString title;
    QString developer;
    QString version;
    QString file_type;
    QString size;
    QString play_time;
    QPixmap icon;
    bool favorite = false;
    bool switch2 = false;
};

struct GameAddOn {
    QString name;
    QString version;
    /// Entry used in Settings::values.disabled_addons when this add-on is off.
    QString key;
    bool is_update = false;
    bool enabled = true;
};

/// Details that need disk access, loaded off the UI thread after the panel opens.
struct GameDetailsExtras {
    u64 program_id = 0;
    QString path;
    std::vector<GameAddOn> add_ons;
    u64 pipeline_cache_bytes = 0;
    bool shaders_precompiled = false;
    bool custom_config = false;
};
