#!/bin/bash


# update from donor Simplicity Studio project
function update_app {
	cp -v $1/hal-config/hal-config.h carb/
	cp -v $1/af-gen-event.h carb/
	cp -v $1/af-structs.h carb/
	cp -v $1/att-storage.h carb/
	cp -v $1/attribute-id.h carb/
	cp -v $1/attribute-size.h carb/
	cp -v $1/attribute-type.h carb/
	cp -v $1/call-command-handler.c carb/
	cp -v $1/call-command-handler.h carb/
	cp -v $1/callback-stub.c carb/
	cp -v $1/callback.h carb/
	cp -v $1/client-command-macro.h carb/
	cp -v $1/cluster-id.h carb/
	cp -v $1/command-id.h carb/
	cp -v $1/debug-printing-test.h carb/
	cp -v $1/debug-printing.h carb/
	cp -v $1/enums.h carb/
	cp -v $1/mbedtls-config-generated.h carb/
	cp -v $1/print-cluster.h carb/
	cp -v $1/stack-handler-stub.c carb/
	cp -v $1/dbch_endpoint_config.h carb/
	cp -v $1/znet-bookkeeping.c carb/
	cp -v $1/znet-bookkeeping.h carb/
	cp -v $1/znet-cli.c carb/
	cp -v $1/znet-mfg-token.h carb/
	sed 's#../../../../../SiliconLabs/SimplicityStudio/v4/developer/sdks/gecko_sdk_suite/v2.7/##' $1/znet-token.h > carb/znet-token.h
	sed 's#../../../../../SiliconLabs/SimplicityStudio/v4/developer/sdks/gecko_sdk_suite/v2.7/##' $1/dbch.h > carb/dbch_znet.h
}

update_app /c/Users/paul.riisenberg/SimplicityStudio/v4_workspace/dbch
