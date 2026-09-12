#include <jni.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include "core/sql/sql.h"

JNIEXPORT jlong JNICALL Java_dev_aplominski_rescaledb_Database_nInit(JNIEnv *env, jclass cls) {
    Db *db = (Db*)calloc(1, sizeof(Db));
    if (!db) return 0;
    extern int db_init(Db *db);
    if (db_init(db) != 0) { free(db); return 0; }
    return (jlong)(uintptr_t)db;
}

JNIEXPORT jint JNICALL Java_dev_aplominski_rescaledb_Database_nExec(JNIEnv *env, jclass cls, jlong handle, jstring sql, jbyteArray out) {
    Db *db = (Db*)(uintptr_t)handle;
    if (!db) return -1;
    const char *csql = (*env)->GetStringUTFChars(env, sql, 0);
    jbyte *cout = (*env)->GetByteArrayElements(env, out, 0);
    jsize cap = (*env)->GetArrayLength(env, out);
    int rc = db_exec(db, csql, (char*)cout, cap);
    (*env)->ReleaseStringUTFChars(env, sql, csql);
    (*env)->ReleaseByteArrayElements(env, out, cout, 0);
    return rc;
}

JNIEXPORT jint JNICALL Java_dev_aplominski_rescaledb_Database_nQuery(JNIEnv *env, jclass cls, jlong handle, jstring sql, jobject res) {
    Db *db = (Db*)(uintptr_t)handle;
    if (!db) return -1;
    const char *csql = (*env)->GetStringUTFChars(env, sql, 0);
    RscResult r;
    memset(&r, 0, sizeof(r));
    int rc = db_query(db, csql, &r);
    (*env)->ReleaseStringUTFChars(env, sql, csql);
    if (rc != 0) return rc;
    jclass clsRes = (*env)->GetObjectClass(env, res);
    jfieldID fidNcols = (*env)->GetFieldID(env, clsRes, "ncols", "I");
    jfieldID fidNrows = (*env)->GetFieldID(env, clsRes, "nrows", "I");
    (*env)->SetIntField(env, res, fidNcols, r.ncols);
    (*env)->SetIntField(env, res, fidNrows, r.nrows);
    jfieldID fidCols = (*env)->GetFieldID(env, clsRes, "cols", "[Ljava/lang/String;");
    jfieldID fidCells = (*env)->GetFieldID(env, clsRes, "cells", "[[Ljava/lang/String;");
    jobjectArray colsArr = (jobjectArray)(*env)->GetObjectField(env, res, fidCols);
    jobjectArray cellsArr = (jobjectArray)(*env)->GetObjectField(env, res, fidCells);
    for (int i = 0; i < r.ncols && i < 16; i++) {
        jstring s = (*env)->NewStringUTF(env, r.cols[i]);
        (*env)->SetObjectArrayElement(env, colsArr, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    for (int rr = 0; rr < r.nrows && rr < 256; rr++) {
        jobjectArray row = (jobjectArray)(*env)->GetObjectArrayElement(env, cellsArr, rr);
        for (int c = 0; c < r.ncols && c < 16; c++) {
            jstring s = (*env)->NewStringUTF(env, r.cells[rr][c]);
            (*env)->SetObjectArrayElement(env, row, c, s);
            (*env)->DeleteLocalRef(env, s);
        }
        (*env)->DeleteLocalRef(env, row);
    }
    return 0;
}

JNIEXPORT void JNICALL Java_dev_aplominski_rescaledb_Database_nClose(JNIEnv *env, jclass cls, jlong handle) {
    Db *db = (Db*)(uintptr_t)handle;
    if (!db) return;
    db_close(db);
    free(db);
}
