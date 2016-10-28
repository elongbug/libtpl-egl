/**
 * @file tpl-test_base.cpp
 * @brief TPLTestBase class functions are defined in this file
 */

#include "tpl-test_base.h"


TPLBackendBase *TPLTestBase::backend = (TPLBackendBase *)NULL;
Config TPLTestBase::config;


void
TPLTestBase::SetUpTestCase()
{
	backend->tpl_backend_initialize(&config);
}


void
TPLTestBase::TearDownTestCase()
{
	backend->tpl_backend_finalize(&config);
}

