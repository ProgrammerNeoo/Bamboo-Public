#include <cmath>
#include <ctime>
#include <random>
#include "txn.h"
#include "row.h"
#include "row_dirty_occ.h"
#include "mem_alloc.h"

#if CC_ALG == DIRTY_OCC

// This function initializes the data structure
void Row_dirty_occ::init(row_t * row) {
    _temp = 0;
    _tid = 0;
    _stashed_tid = 0;
    _stashed_txn = NULL;
    _row = row;
    _stashed_row = NULL;
    _rdm.init(time(NULL));
}

// This function performs a dirty access or a clean access.
// To perform a dirty access (read or write), there are three requirements:
// 1. The row is a hotspot, which is measured by _temp;
// 2. This row was dirty-written by other transactions before (_stash_row not NULL);
// 3. The writer is still running (_stashed_txn->get_txn_id() == _stashed_tid);
// 4. The writer has a smaller txn_id than that of the reader (to avoid deadlock).
RC Row_dirty_occ::access(txn_man * txn, TsType type, row_t * local_row) {
    if (unlikely(_temp >= DR_THRESHOLD && _stashed_row && _stashed_txn
        && _stashed_txn->get_txn_id() == (_stashed_tid & (~LOCK_BIT)))) {
        if ((_stashed_tid & (~LOCK_BIT)) < txn->get_txn_id()) {
            // Dirty access
            // Access the latest uncommitted data
            lock_stashed();
            if (_temp >= DR_THRESHOLD && _stashed_row && _stashed_txn
                && (_stashed_tid & (~LOCK_BIT)) < txn->get_txn_id()
                && _stashed_txn->get_txn_id() == (_stashed_tid & (~LOCK_BIT))) {
                local_row->copy(_stashed_row);
                // Register as dependent to the writer transaction
                _stashed_txn->register_dep(txn);
                txn->inc_dep_cnt();
                txn->last_tid = _stashed_tid & (~LOCK_BIT);
            } else {
                // After the first check, it is possible that the stashed version gets removed
                release_stashed();
                goto clean_access;
            }
            release_stashed();
        }
    } else {
    clean_access:
        // Clean access
        // Access the latest committed data
        ts_t v = 0;
        ts_t v2 = 1;
        while (v2 != v) {
            v = _tid;
            while (v & LOCK_BIT) {
                PAUSE
                v = _tid;
            }
            local_row->copy(_row);
            COMPILER_BARRIER
            v2 = _tid;
        }
        txn->last_tid = v & (~LOCK_BIT);
    }
    return RCOK;
}

// This function increments the current temperature by possibility of 1/2^(_temp)
void Row_dirty_occ::inc_temp() {
    if ((double)_rdm.next() / RAND_MAX <= 1 / pow(2, _temp)) {
        uint64_t temp = _temp;
        while (!__sync_bool_compare_and_swap(&_temp, temp, temp + 1)) {
            PAUSE
            temp = _temp;
        }
    }
}

// This function performs a validation on the current row
bool Row_dirty_occ::validate(ts_t tid, bool in_write_set) {
    ts_t v = _tid;
    if (in_write_set) {
        // If the row is in the write set, then the row has already been locked
        if (tid != (v & (~LOCK_BIT))) {
            abort_cnt_write_mismatch++;
            inc_temp();
            return false;
        } else {
            return true;
        }
    }
    if ((_temp < DR_THRESHOLD) && (v & LOCK_BIT)) {
        // For read set validation on non-hotspot, abort if the tuple is now locked
        abort_cnt_read_locked++;
        inc_temp();
        return false;
    } else if (tid != (v & (~LOCK_BIT))) {
        abort_cnt_read_mismatch++;
        inc_temp();
        return false;
    }
    return true;
}

// This function is invoked to perform a dirty write
void Row_dirty_occ::dirty_write(row_t * data, ts_t tid, txn_man * txn) {
    lock_stashed();
    if (_stashed_row) {
        // Free the previous stashed row
        mem_allocator.free(_stashed_row, sizeof(row_t));
    }
    _stashed_row = data;
    _stashed_txn = txn;
    _stashed_tid = tid | LOCK_BIT;
    release_stashed();
}

// This function is invoked to update rows of successfully committed transactions
void Row_dirty_occ::write(row_t * data, ts_t tid) {
    _row->copy(data);
    ts_t v = _tid;
	M_ASSERT(v & LOCK_BIT, "tid=%ld, v & LOCK_BIT=%ld, v & (~LOCK_BIT)=%ld\n",
            tid, (v & LOCK_BIT), (v & (~LOCK_BIT)));
    _tid = (tid | LOCK_BIT);
}

// This function clears the stashed version if the tid matches
void Row_dirty_occ::clear_stashed(ts_t tid) {
    if (_stashed_row && tid == (_stashed_tid & (~LOCK_BIT))) {
        lock_stashed();
        if (_stashed_row && tid == (_stashed_tid & (~LOCK_BIT))) {
            mem_allocator.free(_stashed_row, sizeof(row_t));
            _stashed_row = NULL;
            _stashed_txn = NULL;
            _stashed_tid = LOCK_BIT;
        }
        release_stashed();
    }
}

void Row_dirty_occ::lock() {
    ts_t v = _tid;
    while ((v & LOCK_BIT) || !__sync_bool_compare_and_swap(&_tid, v, v | LOCK_BIT)) {
		PAUSE
		v = _tid;
	}
}

void Row_dirty_occ::release() {
    assert_lock();
    _tid &= (~LOCK_BIT);
}

void Row_dirty_occ::lock_stashed() {
    ts_t v = _stashed_tid;
    while ((v & LOCK_BIT) || !__sync_bool_compare_and_swap(&_stashed_tid, v, v | LOCK_BIT)) {
        PAUSE
        v = _stashed_tid;
    }
}

void Row_dirty_occ::release_stashed() {
    _stashed_tid &= (~LOCK_BIT);
}

bool Row_dirty_occ::try_lock() {
    ts_t v = _tid;
    if (v & LOCK_BIT) {
        return false;
    }
    return __sync_bool_compare_and_swap(&_tid, v, (v | LOCK_BIT));
}

ts_t Row_dirty_occ::get_tid() {
    return _tid & (~LOCK_BIT);
}

#endif
