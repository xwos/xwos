#! /bin/make -f
# @file
# @brief 查找中间件模块并生成Makefile目标
# @author
# + 隐星魂 (Roy.Sun) <https://xwos.tech>
# @copyright
# + (c) 2015 隐星魂 (Roy.Sun) <https://xwos.tech>
# > Licensed under the Apache License, Version 2.0 (the "License");
# > you may not use this file except in compliance with the License.
# > You may obtain a copy of the License at
# >
# >         http://www.apache.org/licenses/LICENSE-2.0
# >
# > Unless required by applicable law or agreed to in writing, software
# > distributed under the License is distributed on an "AS IS" BASIS,
# > WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# > See the License for the specific language governing permissions and
# > limitations under the License.
#

include xwbs/util/mk/xwmo.mk

XWMD_LIST := $(shell test -d $(XWOS_MD_DIR) && \
                     find $(XWOS_MD_DIR) -type f -name "xwmo.mk" -exec dirname {} \;)

define findXwmdTarget
ifeq ($$(XWMDCFG$(call xwmoPathToName,$(1),$(XWOS_MD_DIR))),y)
    XWMD += $(1)/xwmd$(call xwmoPathToName,$(1),$(XWOS_MD_DIR)).a
    XWMD_DSM += $(1)/xwmd$(call xwmoPathToName,$(1),$(XWOS_MD_DIR)).a.dsm
    XWMD_CLEAN += $(1)/xwmd$(call xwmoPathToName,$(1),$(XWOS_MD_DIR)).a.clean
    XWMD_DISTCLEAN += $(1)/xwmd$(call xwmoPathToName,$(1),$(XWOS_MD_DIR)).a.distclean
endif
endef

$(foreach xwmd,$(XWMD_LIST),$(eval $(call findXwmdTarget,$(xwmd))))

XWMD := $(addprefix $(OBJ_DIR),$(XWMD))
XWMD_DSM := $(addprefix $(OBJ_DIR),$(XWMD_DSM))
XWMD_CLEAN := $(addprefix $(OBJ_DIR),$(XWMD_CLEAN))
XWMD_DISTCLEAN := $(addprefix $(OBJ_DIR),$(XWMD_DISTCLEAN))

$(XWMD):
	@echo "building $@ ..."
	@$(MAKE) -C $(XWOS_PATH) -f $(subst $(OBJ_DIR),,$(@D))/xwmo.mk XWOS_BRD_DIR=$(XWOS_BRD_DIR)

$(XWMD_DSM):
	@$(MAKE) -C $(XWOS_PATH) -f $(subst $(OBJ_DIR),,$(@D))/xwmo.mk XWOS_BRD_DIR=$(XWOS_BRD_DIR) NODEP=y dsm

$(XWMD_CLEAN):
	@$(MAKE) -C $(XWOS_PATH) -f $(subst $(OBJ_DIR),,$(@D))/xwmo.mk XWOS_BRD_DIR=$(XWOS_BRD_DIR) NODEP=y clean

$(XWMD_DISTCLEAN):
	@$(MAKE) -C $(XWOS_PATH) -f $(subst $(OBJ_DIR),,$(@D))/xwmo.mk XWOS_BRD_DIR=$(XWOS_BRD_DIR) NODEP=y distclean

xwmd.mk.dbg:
	@echo XWMD_LIST:$(XWMD_LIST)
	@echo XWMD:$(XWMD)

.PHONY: xwmd.mk.dbg
