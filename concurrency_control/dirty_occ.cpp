#include "txn.h"
#include "row.h"
#include "row_dirty_occ.h"

#if CC_ALG == DIRTY_OCC

RC txn_man::validate_dirty_occ() {
    RC rc = RCOK;

    // Get the write set and the read set
    int write_set[wr_cnt];
    int cur_wr_idx = 0;
    int read_set[row_cnt - wr_cnt];
    int cur_rd_idx = 0;
    for (int rid = 0; rid < row_cnt; rid++) {
        if (accesses[rid]->type == WR)
            write_set[cur_wr_idx++] = rid;
        else
            read_set[cur_rd_idx++] = rid;
    }

    // Bubble sort the write set to be in the primary key order
    for (int i = 0; i < wr_cnt - 1; i++) {
        for (int j = 0; j < wr_cnt - i - 1; j++) {
            if (accesses[write_set[j]]->orig_row->get_primary_key() >
                accesses[write_set[j + 1]]->orig_row->get_primary_key()) {
                int tmp = write_set[j];
                write_set[j] = write_set[j + 1];
                write_set[j + 1] = tmp;
            }
        }
    }

    // Lock tuples in the write set in the primary key order
    int num_locks = 0;
    bool done = false;
    if (_validation_no_wait) {
        while (!done) {
            num_locks = 0;
            for (int i = 0; i < wr_cnt; i++) {
                row_t * row = accesses[write_set[i]]->orig_row;
                if (!row->manager->try_lock()) {
                    break;
                }
                row->manager->assert_lock();
                num_locks++;
                if (row->manager->get_tid() != accesses[write_set[i]]->tid) {
                    rc = Abort;
                    goto final;
                }
            }
            if (num_locks == wr_cnt) {
                done = true;
            } else {
                for (int i = 0; i < num_locks; i++) {
                    accesses[write_set[i]]->orig_row->manager->release();
                }
            }
            PAUSE
        }
    } else {
        for (int i = 0; i < wr_cnt; i++) {
            row_t * row = accesses[write_set[i]]->orig_row;
            row->manager->lock();
            num_locks++;
            if (row->manager->get_tid() != accesses[write_set[i]]->tid) {
                rc = Abort;
                goto final;
            }
        }
    }

    // Validate tuples in the read set
    for (int i = 0; i < row_cnt - wr_cnt; i++) {
        Access * access = accesses[read_set[i]];
        bool success = access->orig_row->manager->validate(access->tid, false);
        if (!success) {
            rc = Abort;
            goto final;
        }
    }

    // Validate tuples in the write set
    for (int i = 0; i < wr_cnt; i++) {
        Access * access = accesses[write_set[i]];
        bool success = access->orig_row->manager->validate(access->tid, true);
        if (!success) {
            rc = Abort;
            goto final;
        }
    }

final:
    if (rc == Abort) {
        for (int i = 0; i < num_locks; i++) {
            accesses[write_set[i]]->orig_row->manager->release();
        }
        cleanup(rc);
    } else {
        for (int i = 0; i < wr_cnt; i++) {
            Access * access = accesses[write_set[i]];
            access->orig_row->manager->write(access->data, _tid);
            access->orig_row->manager->release();
        }
        cleanup(rc);
    }

    return rc;
}

#endif