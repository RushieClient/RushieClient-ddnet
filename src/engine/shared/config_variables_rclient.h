// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

// Dummy change clan
MACRO_CONFIG_STR(RcPlayerClanNoDummy, rc_player_clan_no_dummy, 12, "#NODUMMY", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Thats is clan when dummy not connected")
MACRO_CONFIG_STR(RcPlayerClanWithDummy, rc_player_clan_with_dummy, 12, "#YESDUMMY", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Thats is clan when dummy connected")
MACRO_CONFIG_INT(RcPlayerClanAutoChange, rc_player_clan_auto_change, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change clan when dummy connected")

// Binds
MACRO_CONFIG_INT(RcToggle45degrees, rc_toggle_45_degrees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle 45 degrees bind or not")
MACRO_CONFIG_INT(Rc45degreesEcho, rc_45_degrees_echo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "45 degrees echo")
MACRO_CONFIG_INT(RcToggleSmallSens, rc_toggle_small_sens, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle small sens bind or not")
MACRO_CONFIG_INT(RcSmallSensEcho, rc_small_sens_echo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Small sens echo")
MACRO_CONFIG_INT(RcDeepFlyOnRMB, rc_deep_fly_on_rmb, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Deepfly on right mouse button")

// Master servers
MACRO_CONFIG_INT(RcUseRushieMasterServerMirrors, rc_use_rushie_master_server_mirrors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use Rushie master server mirrors")
MACRO_CONFIG_INT(RcUseBestClientMasterServerMirrors, rc_use_bestclient_master_server_mirrors, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use BestClient master server mirrors")
MACRO_CONFIG_INT(RcFilterOnlyEmptyServers, rc_filter_only_empty_servers, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use master server mirrors")

// Chat Filter
MACRO_CONFIG_INT(RcMessageFilterMode, rc_message_filter_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Message filter mode(0-off,1-partial,2-full,3-both")
MACRO_CONFIG_INT(RcMessageFilterPrintBlockedMessage, rc_message_filter_print_blocked_message, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Print in console blocked message")
MACRO_CONFIG_INT(RcMessageFilterMultiplyChangeWordOnFullMatch, rc_message_filter_multiply_change_word_on_full_match, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Multiply count of change word on full match(0-off, 1-on)")
MACRO_CONFIG_STR(RcMessageFilterWordOnFullMatch, rc_message_filter_word_on_full_match, 64, "^", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Word when censor full match")
MACRO_CONFIG_INT(RcMessageFilterMultiplyChangeWordOnPartialMatch, rc_message_filter_multiply_change_word_on_partial_match, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Multiply count of change word on partial match(0-off, 1-on)")
MACRO_CONFIG_STR(RcMessageFilterWordOnPartialMatch, rc_message_filter_word_on_partial_match, 64, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Word when censor partial match")
MACRO_CONFIG_COL(RcMessageFilterPrintBlockedMessageColor, rc_message_filter_print_blocked_message_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of blocked message in console")

// Translate
MACRO_CONFIG_INT(RcTranslateSend, rc_translate_send, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Translate sending message")
MACRO_CONFIG_STR(RcTranslateSendTarget, rc_translate_send_target, 16, "en", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sending message translate target language (must be 2 character ISO 639 code)")

// Heart
MACRO_CONFIG_INT(RcShowHeartInScoreboard, rc_show_heart_in_scoreboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show heart in scoreboard")
MACRO_CONFIG_INT(RcSizeOfHeart, rc_size_of_heart, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Heart size")

// Scoreboard
MACRO_CONFIG_INT(RcScoreboardAlwaysShowQuickActions, rc_scoreboard_always_show_quick_actions, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show quick actions in scoreboard always")
MACRO_CONFIG_INT(RcScoreboardFreezeInputs, rc_scoreboard_freeze_inputs, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze inputs when unlock mouse")

// Chatbubbles
MACRO_CONFIG_INT(RcChatBubbles, rc_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles above players")
MACRO_CONFIG_INT(RcChatBubbleSize, rc_chat_bubble_size, 20, 15, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the chat bubble")
MACRO_CONFIG_INT(RcChatBubbleShowTime, rc_chat_bubble_showtime, 200, 200, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long to show the bubble for")
MACRO_CONFIG_INT(RcChatBubbleFadeOut, rc_chat_bubble_fadeout, 35, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long it fades out")
MACRO_CONFIG_INT(RcChatBubbleFadeIn, rc_chat_bubble_fadein, 15, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "how long it fades in")

// EdgeInfo
MACRO_CONFIG_COL(RcEdgeInfoColorFreeze, rc_edge_info_color_freeze, 9930605, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze color in edge info")
MACRO_CONFIG_COL(RcEdgeInfoColorKill, rc_edge_info_color_kill, 65461, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Kill color in edge info")
MACRO_CONFIG_COL(RcEdgeInfoColorSafe, rc_edge_info_color_safe, 5594535, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Safe color in edge info")
MACRO_CONFIG_INT(RcEdgeInfoCords, rc_edge_info_cords, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show upper panel of edge info")
MACRO_CONFIG_INT(RcEdgeInfoJump, rc_edge_info_jump, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show lower panel of edge info")
MACRO_CONFIG_INT(RcEdgeInfoPosX, rc_edge_info_pos_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change edge info pos x")
MACRO_CONFIG_INT(RcEdgeInfoPosY, rc_edge_info_pos_y, 56, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change edge info pos y")

// Hud
MACRO_CONFIG_INT(RcShowhudPlayerCheckpoint, rc_showhud_player_checkpoint, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show checkpoint in hud")
MACRO_CONFIG_INT(RcShowhudSmallerHud, rc_showhud_smaller_hud, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "checkpoint and hud will be oneline")
MACRO_CONFIG_INT(RcShowhudAdvancedDummyActions, rc_showhud_advanced_dummy_actions, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show advanced ingame HUD (Dummy Actions)")

// Nameplates
MACRO_CONFIG_INT(RcNamePlatesHook, rc_nameplates_hook, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hook detection in name plates (1 = other players', 2 = everyone, 3 = only your own, 4 = your and dummy")
MACRO_CONFIG_INT(RcNamePlatesHookSize, rc_nameplates_hook_size, 30, -50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of hook detection icons")
MACRO_CONFIG_INT(RcNamePlatesHookShiftOnInvis, rc_nameplates_hook_shift_on_invis, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook when not visible will still take up space")
MACRO_CONFIG_INT(RcNamePlatesFire, rc_nameplates_fire, 0, 0, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show fire detection in name plates (1 = other players'(fake), 2 = everyone(fake), 3 = only your own, 4 = your and dummy")
MACRO_CONFIG_INT(RcNamePlatesFireSize, rc_nameplates_fire_size, 30, -50, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of fire detection icons")
MACRO_CONFIG_INT(RcNamePlatesFireShiftOnInvis, rc_nameplates_fire_shift_on_invis, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fire when not visible will still take up space")

// Commands
MACRO_CONFIG_INT(RcCommandsFixLayout, rc_commands_fix_layout, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "checkpoint and hud will be oneline")

// Players
MACRO_CONFIG_INT(RcHideFrozenFlakesEffect, rc_hide_frozen_flakes_effect, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide frozen flakes effect")
MACRO_CONFIG_INT(RcShowSparkleEffect, rc_show_sparkle_effect, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Always show sparkle effect")
MACRO_CONFIG_INT(RcShowAfkEmoteInMenu, rc_show_afk_emote_menu, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows afk emote when player in menu (only client)")
MACRO_CONFIG_INT(RcShowAfkTextureInMenu, rc_show_afk_texture_in_menu, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows afk texture when player in menu (only client)")
MACRO_CONFIG_INT(RcShowAfkEmoteInSpec, rc_show_afk_emote_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows afk emote when player in spec (only client)")
MACRO_CONFIG_INT(RcShowAfkTextureInSpec, rc_show_afk_texture_in_spec, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows spec texture when player in spec (only client)")

// Menus flags
MACRO_CONFIG_INT(RcRClientSettingsTabs, rc_rclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags to disable settings tabs")

// Anti AFK
MACRO_CONFIG_INT(RcPlayOnMoveNonInactive, rc_play_on_move_nonactive, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Play sound when someone moves u when u inactive")
MACRO_CONFIG_INT(RcSoundOnMoveNonInactive, rc_sound_on_move_nonactive, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Choose sound when inactive (0-WakeUp, 1-Grenade boom, 2-msg tag")
MACRO_CONFIG_INT(RcNotifyOnMoveInSpec, rc_notify_on_move_in_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show when someone moves u when u spectate")
MACRO_CONFIG_INT(RcPlayOnMoveInSpec, rc_play_on_move_in_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Play sound when someone moves u when u spectate")
MACRO_CONFIG_INT(RcSoundOnMoveInSpec, rc_sound_on_move_in_spec, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Choose sound when spectate (0-WakeUp, 1-Grenade boom, 2-msg tag")

// Custom Client
MACRO_CONFIG_INT(RcCustomClientsCollectClientType, rc_custom_clients_collect_client_type, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Collect client types on server")
MACRO_CONFIG_INT(RcCustomClientsSendClientType, rc_custom_clients_send_client_type, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Send client type on server")
MACRO_CONFIG_INT(RcCustomClientsInNameplates, rc_custom_clients_in_nameplates, 1, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show custom client icons on nameplates")
MACRO_CONFIG_INT(RcCustomClientsInScoreboard, rc_custom_clients_in_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show custom client icons on scoreboard")

// Streamer Mode
MACRO_CONFIG_INT(RcRconSteamerMode, rc_rcon_streamer_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide ddnet from stream when rcon opened")