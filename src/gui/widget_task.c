﻿/* ****//***********************************************************************
 * widget_task.c -- LCUI widget task module.
 *
 * Copyright (C) 2014-2016 by Liu Chao <lc-soft@live.cn>
 *
 * This file is part of the LCUI project, and may only be used, modified, and
 * distributed under the terms of the GPLv2.
 *
 * (GPLv2 is abbreviation of GNU General Public License Version 2)
 *
 * By continuing to use, modify, or distribute this file you indicate that you
 * have read the license and understand and accept it fully.
 *
 * The LCUI project is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GPL v2 for more details.
 *
 * You should have received a copy of the GPLv2 along with this file. It is
 * usually in the LICENSE.TXT file, If not, see <http://www.gnu.org/licenses/>.
 * ***************************************************************************/

/* ****************************************************************************
 * widget_task.c -- LCUI部件任务处理模块
 *
 * 版权所有 (C) 2014-2016 归属于 刘超 <lc-soft@live.cn>
 *
 * 这个文件是LCUI项目的一部分，并且只可以根据GPLv2许可协议来使用、更改和发布。
 *
 * (GPLv2 是 GNU通用公共许可证第二版 的英文缩写)
 *
 * 继续使用、修改或发布本文件，表明您已经阅读并完全理解和接受这个许可协议。
 *
 * LCUI 项目是基于使用目的而加以散布的，但不负任何担保责任，甚至没有适销性或特
 * 定用途的隐含担保，详情请参照GPLv2许可协议。
 *
 * 您应已收到附随于本文件的GPLv2许可协议的副本，它通常在LICENSE.TXT文件中，如果
 * 没有，请查看：<http://www.gnu.org/licenses/>.
 * ***************************************************************************/

//#define DEBUG
#include <stdio.h>
#include <stdlib.h>
#include <LCUI_Build.h>
#include <LCUI/LCUI.h>
#include <LCUI/gui/widget.h>

static void HandleRefreshStyle( LCUI_Widget w )
{
	Widget_ExecUpdateStyle( w, TRUE );
	w->task.buffer[WTT_UPDATE_STYLE] = FALSE;
	w->task.buffer[WTT_CACHE_STYLE] = TRUE;
}

static void HandleUpdateStyle( LCUI_Widget w )
{
	Widget_ExecUpdateStyle( w, FALSE );
	w->task.buffer[WTT_CACHE_STYLE] = TRUE;
}

static void HandleCacheStyle( LCUI_Widget w )
{
	StyleSheet_Clear( w->cached_style );
	StyleSheet_Replace( w->cached_style, w->style );
}

static void HandleSetTitle( LCUI_Widget w )
{
	Widget_PostSurfaceEvent( w, WET_TITLE );
}

/** 处理主体刷新（标记主体区域为脏矩形，但不包括阴影区域） */
static void HandleBody( LCUI_Widget w )
{
	Widget_InvalidateArea( w, NULL, SV_BORDER_BOX );
}

/** 处理刷新（标记整个部件区域为脏矩形） */
static void HandleRefresh( LCUI_Widget w )
{
	DEBUG_MSG( "refresh\n" );
	Widget_InvalidateArea( w, NULL, SV_GRAPH_BOX );
}

/** 更新当前任务状态，确保部件的任务能够被处理到 */
void Widget_UpdateTaskStatus( LCUI_Widget widget )
{
	int i;
	for( i = 0; i < WTT_TOTAL_NUM; ++i ) {
		if( widget->task.buffer[i] ) {
			widget->task.for_self = TRUE;
		}
	}
	widget = widget->parent;
	while( widget ) {
		widget->task.for_children = TRUE;
		widget = widget->parent;
	}
}

void Widget_AddTaskForChildren( LCUI_Widget widget, int task )
{
	LCUI_Widget child;
	LinkedListNode *node;
	widget->task.for_children = TRUE;
	LinkedList_ForEach( node, &widget->children ) {
		child = node->data;
		Widget_AddTask( child, task );
		Widget_AddTaskForChildren( child, task );
	}
}

void Widget_AddTask( LCUI_Widget widget, int task )
{
	if( widget->state == WSTATE_DELETED ) {
		return;
	}
	widget->task.for_self = TRUE;
	widget->task.buffer[task] = TRUE;
	widget = widget->parent;
	/* 向没有标记的父级部件添加标记 */
	while( widget && !widget->task.for_children ) {
		widget->task.for_children = TRUE;
		widget = widget->parent;
	}
}

typedef void (*callback)(LCUI_Widget);

static callback task_handlers[WTT_TOTAL_NUM];

/** 映射任务处理器 */
static void MapTaskHandler(void)
{
	task_handlers[WTT_VISIBLE] = Widget_UpdateVisibility;
	task_handlers[WTT_POSITION] = Widget_UpdatePosition;
	task_handlers[WTT_RESIZE] = Widget_UpdateSize;
	task_handlers[WTT_SHADOW] = Widget_UpdateBoxShadow;
	task_handlers[WTT_BORDER] = Widget_UpdateBorder;
	task_handlers[WTT_OPACITY] = Widget_UpdateOpacity;
	task_handlers[WTT_BODY] = HandleBody;
	task_handlers[WTT_TITLE] = HandleSetTitle;
	task_handlers[WTT_REFRESH] = HandleRefresh;
	task_handlers[WTT_UPDATE_STYLE] = HandleUpdateStyle;
	task_handlers[WTT_REFRESH_STYLE] = HandleRefreshStyle;
	task_handlers[WTT_CACHE_STYLE] = HandleCacheStyle;
	task_handlers[WTT_BACKGROUND] = Widget_UpdateBackground;
	task_handlers[WTT_LAYOUT] = Widget_ExecUpdateLayout;
	task_handlers[WTT_ZINDEX] = Widget_ExecUpdateZIndex;
	task_handlers[WTT_PROPS] = Widget_UpdateProps;
}

/** 初始化 LCUI 部件任务处理功能 */
void LCUIWidget_InitTask(void)
{
	MapTaskHandler();
}

/** 销毁（释放） LCUI 部件任务处理功能的相关资源 */
void LCUIWidget_ExitTask(void)
{

}

/** 处理部件中当前积累的任务 */
int Widget_Update( LCUI_Widget w )
{
	int ret = 1, i;
	LCUI_BOOL *buffer;
	LCUI_Widget child;
	LinkedListNode *node, *next;

	/* 如果该部件有任务需要处理 */
	if( w->task.for_self ) {
		ret = LCUIMutex_TryLock( &w->mutex );
		if( ret != 0 ) {
			ret = 1;
			goto skip_proc_self_task;
		}
		w->task.for_self = FALSE;
		buffer = w->task.buffer;
		/* 如果有用户自定义任务 */
		if( buffer[WTT_USER] ) {
			LCUI_WidgetClass *wc;
			wc = LCUIWidget_GetClass( w->type );
			wc ? wc->task_handler( w ) : FALSE;
		}
		for( i = 0; i < WTT_USER; ++i ) {
			if( buffer[i] ) {
				buffer[i] = FALSE;
				if( task_handlers[i] ) {
					task_handlers[i]( w );
				}
			} else {
				buffer[i] = FALSE;
			}
		}
		LCUIMutex_Unlock( &w->mutex );
		/* 如果部件还处于未准备完毕的状态 */
		if( w->state < WSTATE_READY ) {
			w->state |= WSTATE_UPDATED;
			/* 如果部件已经准备完毕则触发 ready 事件 */
			if( w->state == WSTATE_READY ) {
				LCUI_WidgetEventRec e;
				e.type = WET_READY;
				e.cancel_bubble = TRUE;
				Widget_TriggerEvent( w, &e, NULL );
				w->state = WSTATE_NORMAL;
			}
		}
	}
	/* 删除无用部件 */
	node = w->children_trash.head.next;
	while( node ) {
		next = node->next;
		LinkedList_Unlink( &w->children_trash, node );
		Widget_ExecDestroy( node->data );
		node = next;
	}

skip_proc_self_task:;

	/* 如果子级部件中有待处理的部件，则递归进去 */
	if( w->task.for_children ) {
		w->task.for_children = FALSE;
		node = w->children.head.next;
		while( node ) {
			child = node->data;
			/* 如果当前部件有销毁任务，结点空间会连同部件一起被
			 * 释放，为避免因访问非法空间而出现异常，预先保存下
			 * 个结点。
			 */
			next = node->next;
			ret = Widget_Update( child );
			/* 如果该级部件的任务需要留到下次再处理 */
			if( ret == 1 ) {
				w->task.for_children = TRUE;
			}
			node = next;
		}
	}
	return (w->task.for_self || w->task.for_children) ? 1 : 0;
}

/** 处理一次当前积累的部件任务 */
void LCUIWidget_StepTask(void)
{
	Widget_Update( LCUIWidget_GetRoot() );
}
