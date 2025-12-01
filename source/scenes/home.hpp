#pragma once

void Home_init(void);

void Home_exit(void);

void Home_suspend(void);

void Home_resume(std::string arg);

void Home_rebuild_channels_tab(void);

void Home_rebuild_feed_tab(void);

void Home_update_pull_to_refresh(void);

void Home_draw(void);
