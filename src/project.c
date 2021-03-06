/*
** Copyright (c) 2010 Michael Dvorkin
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
*/
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "pit.h"

static bool project_already_exist(char *name)
{
    pit_db_load();
    for_each_project(pp) {
        if (!strcmp(pp->name, name)) {
            return TRUE;
        }
    }
    return FALSE;
}

static int project_find_current(int id, PProject *ppp)
{
    if (id) {
        *ppp = (PProject)pit_table_find(projects, id);
        if (!*ppp) die("could not find project %d", id);
    } else {
        *ppp = (PProject)pit_table_current(projects);
        if (!*ppp) die("could not find current project");
    }
    return *ppp ? (*(PProject *)ppp)->id : 0;
}

static void project_log_create(PProject pp)
{
    Action a = { pp->id, 0 };

    sprintf(a.message, "created project %d: %s (status: %s)", pp->id, pp->name, pp->status);
    pit_action(&a);
}

static void project_log_update(PProject pp, POptions po)
{
    Action a = { pp->id, 0 };
    bool empty = TRUE;

    sprintf(a.message, "updated project %d:", pp->id);
    if (po->project.name) {
        sprintf(a.message + strlen(a.message), " (name: %s", pp->name);
        empty = FALSE;
    } else {
        sprintf(a.message + strlen(a.message), " %s (", pp->name);
    }
    if (po->project.status) {
        sprintf(a.message + strlen(a.message), "%sstatus: %s", (empty ? "" : ", "), pp->status);
    }
    strcat(a.message, ")");
    pit_action(&a);
}

static void project_log_delete(int id, char *name, int number_of_tasks)
{
    Action a = { id, 0 };

    sprintf(a.message, "deleted project %d: %s", id, name);
    if (number_of_tasks > 0) {
        sprintf(a.message + strlen(a.message), " with %d task%s", number_of_tasks, (number_of_tasks == 1 ? "" : "s"));
    }
    pit_action(&a);
}

static void project_list(POptions po)
{
    PFormat pf;

    pit_db_load();
    if (projects->number_of_records > 0) {
        pf = pit_format_initialize(FORMAT_PROJECT, 0, projects->number_of_records);
        for_each_project(pp) {
            if ((po->project.name && !stristr(pp->name, po->project.name)) ||
               (po->project.status && !stristr(pp->status, po->project.status)))
               continue;
            pit_format(pf, (char *)pp);
        }
        pit_format_flush(pf);
    }
}

static void project_show(int id)
{
    PProject pp;

    pit_db_load();
    id = project_find_current(id, &pp);

    if (pp) {
        /* printf("The project was created on %s, last updated on %s\n", format_timestamp(pp->created_at), format_timestamp(pp->updated_at)); */
        printf("* %d: (%s) %s (status: %s, %d task%s)\n", 
            pp->id, pp->username, pp->name, pp->status, pp->number_of_tasks, pp->number_of_tasks != 1 ? "s" : "");
        pit_table_mark(projects, pp->id);
        if (pp->number_of_tasks > 0)
            pit_task_list(NULL, pp);
        pit_db_save();
    } else {
        die("could not find the project");
    }
}

static void project_create(POptions po)
{
    pit_db_load();

    if (project_already_exist(po->project.name)) {
        die("project with the same name already exists");
    } else {
        Project p = { 0 }, *pp;

        if (!po->project.status) po->project.status = "active";

        strncpy(p.name,     po->project.name,   sizeof(p.name)     - 1);
        strncpy(p.status,   po->project.status, sizeof(p.status)   - 1);
        strncpy(p.username, current_user(),     sizeof(p.username) - 1);

        pp = (PProject)pit_table_insert(projects, (char *)&p);
        pit_table_mark(projects, pp->id);

        project_log_create(pp);
        pit_db_save();
    }
}

static void project_update(int id, POptions po)
{
    PProject pp;

    pit_db_load();
    id = project_find_current(id, &pp);

    if (po->project.name)   strncpy(pp->name,   po->project.name,   sizeof(pp->name)   - 1);
    if (po->project.status) strncpy(pp->status, po->project.status, sizeof(pp->status) - 1);
    strncpy(pp->username, current_user(), sizeof(pp->username) - 1);
    pit_table_mark(projects, pp->id);

    project_log_update(pp, po);
    pit_db_save();
}

static void project_delete(int id)
{
    PProject pp;

    pit_db_load();
    id = project_find_current(id, &pp);
    /*
    ** Delete project tasks.
    */
    if (pp->number_of_tasks > 0) {
        for_each_task(pt) {
            if (pt->project_id == id) {
                pit_task_delete(pt->id, pp);
                --pt; /* Make the task pointer stay since it now points to the next task. */
            }
        }
    }
    /*
    ** Ready to delete the project itself. But first preserve the
    ** name and number of tasks since we need these bits for logging.
    */
    char *deleted_name = str2str(pp->name);
    int deleted_number_of_tasks = pp->number_of_tasks;

    pp = (PProject)pit_table_delete(projects, id);
    if (pp) {
        pit_table_mark(projects, 0); /* TODO: find better current project candidate. */
        project_log_delete(id, deleted_name, deleted_number_of_tasks);
        pit_db_save();
    } else {
        die("could not delete the project");
    }
}

static void project_parse_options(int cmd, char **arg, POptions po)
{
    while(*++arg) {
        switch(pit_arg_option(arg)) {
        case 'n':
            po->project.name = pit_arg_string(++arg, "project name");
            break;
        case 's':
            po->project.status = pit_arg_string(++arg, "project status");
            break;
        default:
            die("invalid project option: %s", *arg);
        }
    }
}

/*
** CREATING PROJECTS:
**   pit project -c name [-s status]
**
** EDITING PROJECTS:
**   pit project -e [number] [-n name] [-s status]
**
** DELETING PROJECTS:
**   pit project -d [number]
**
** VIEWING PROJECT:
**   pit project [[-q] number]
**
** LISTING PROJECTS:
**   pit project -q [number | [-n name] [-s status]]
*/
void pit_project(char *argv[])
{
    char **arg = &argv[1];
    int number = 0;
    Options opt = {{ 0 }};

    if (!*arg) {
        project_list(&opt); /* Show all projects. */
    } else { /* pit project [number] */
        number = pit_arg_number(arg, NULL);
        if (number) {
            project_show(number);
        } else {
            int cmd = pit_arg_option(arg);
            switch(cmd) {
            case 'c': /* pit project -c name [-s status] */
                opt.project.name = pit_arg_string(++arg, "project name");
                project_parse_options(cmd, arg, &opt);
                project_create(&opt);
                break;
            case 'e': /* pit project -e [number] [-n name] [-s status] */
                number = pit_arg_number(++arg, NULL);
                if (!number) --arg;
                project_parse_options(cmd, arg, &opt);
                if (is_zero((char *)&opt.project, sizeof(opt.project))) {
                    die("nothing to update");
                } else {
                    project_update(number, &opt);
                }
                break;
            case 'd': /* pit project -d [number] */
                number = pit_arg_number(++arg, NULL);
                project_delete(number);
                break;
            case 'q': /* pit project -q [number | [-n name] [-s status]] */
                number = pit_arg_number(++arg, NULL);
                if (number) {
                    project_show(number);
                } else {
                    project_parse_options(cmd, --arg, &opt);
                    if (is_zero((char *)&opt.project, sizeof(opt.project))) {
                        project_show(0); /* Show current project if any. */
                    } else {
                        project_list(&opt);
                    }
                }
                break;
            default:
                die("invalid project option: %s", *arg);
            }
        }
    }
}
