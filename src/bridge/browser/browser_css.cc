#include "src/bridge/browser/browser_assets.h"
#include "src/bridge/browser/browser_font_families.h"

namespace moe::bridge {

std::string browser_css() {
  return std::string(R"CSS(:root {
  color-scheme: dark;
  font-family: )CSS") +
         browser::TERMINAL_FONT_FAMILY + R"CSS(;
}

html,
body,
#workspace {
  height: 100%;
  margin: 0;
}

body {
  background: #0b0d0e;
  color: #d9e2df;
}

#workspace {
  display: grid;
  grid-template-rows: 1fr 24px;
}

#surface,
#terminal,
#pane-root,
#pane-preview-root,
#worktree-overlay-background,
.pane-node,
.pane-terminal {
  min-height: 0;
  min-width: 0;
}

#surface {
  overflow: hidden;
  position: relative;
}

#terminal,
#pane-root {
  inset: 0;
  position: absolute;
}

#terminal {
  z-index: 3;
}

#terminal .xterm-viewport {
  background-color: transparent;
}

#pane-staging {
  height: 100%;
  inset: 0;
  pointer-events: none;
  position: absolute;
  visibility: hidden;
  width: 100%;
}

#pane-root {
  visibility: hidden;
  z-index: 1;
}

#pane-preview-root {
  background: #0b0d0e;
  pointer-events: none;
  position: absolute;
  visibility: hidden;
  z-index: 4;
}

#worktree-overlay-background {
  background: #0b0d0e;
  pointer-events: none;
  position: absolute;
  visibility: hidden;
  z-index: 2;
}

#surface.pane-view-active #terminal {
  pointer-events: none;
  visibility: hidden;
}

#surface.pane-view-active #pane-root {
  visibility: visible;
}

#surface.pane-overlay-background-active #pane-root {
  visibility: visible;
}

#surface.worktree-overlay-background-active #worktree-overlay-background {
  visibility: visible;
}

#surface.pane-preview-active #pane-preview-root {
  visibility: visible;
}

#pane-root > .pane-node,
#pane-preview-root > .pane-node {
  height: 100%;
  width: 100%;
}

.pane-node {
  box-sizing: border-box;
  overflow: hidden;
  position: relative;
}

.pane-split {
  display: flex;
  height: 100%;
  width: 100%;
}

.pane-split-left-to-right {
  flex-direction: row;
}

.pane-split-top-to-bottom {
  flex-direction: column;
}

.pane-split-left-to-right > .pane-node:not(:last-child) {
  border-right: 1px solid #27302d;
}

.pane-split-top-to-bottom > .pane-node:not(:last-child) {
  border-bottom: 1px solid #27302d;
}

.pane-terminal {
  height: 100%;
  width: 100%;
}

.pane-leaf::before {
  background: transparent;
  content: "";
  inset: 0;
  pointer-events: none;
  position: absolute;
  transition: background-color 80ms linear;
  z-index: 1;
}

.pane-leaf.pane-muted::before {
  background: rgba(5, 8, 7, 0.45);
}

.pane-node::after {
  border: 2px solid transparent;
  box-sizing: border-box;
  content: "";
  inset: 0;
  pointer-events: none;
  position: absolute;
  z-index: 2;
}

.pane-selected::after {
  border-color: #c58b32;
}

.pane-selection-active::after {
  border-color: #5b9bd5;
}

.pane-move-source::after {
  border-color: #d78700;
}

.pane-move-preview::after {
  border-color: #5faf5f;
}

.pane-move-target::after {
  border-color: #0087af;
}

.pane-focused:not(.pane-selected):not(.pane-move-source)::after {
  border-color: #3f4c48;
  border-width: 1px;
}

.pane-hierarchy-group::after {
  box-shadow: inset 0 0 0 1px rgba(91, 155, 213, 0.18);
}

.pane-hierarchy-active::after {
  box-shadow: inset 0 0 0 1px rgba(91, 155, 213, 0.5);
}

.pane-hierarchy-active::before {
  background: rgba(11, 13, 14, 0.82);
  border: 1px solid rgba(91, 155, 213, 0.65);
  border-radius: 3px;
  color: #8db7dc;
  font-size: 11px;
  line-height: 16px;
  min-width: 16px;
  padding: 0 2px;
  pointer-events: none;
  position: absolute;
  right: 3px;
  text-align: center;
  top: 3px;
  z-index: 3;
}

.pane-split-left-to-right.pane-hierarchy-active::before {
  content: "\2194";
}

.pane-split-top-to-bottom.pane-hierarchy-active::before {
  content: "\2195";
}

#status {
  align-content: center;
  border-top: 1px solid #27302d;
  color: #9fb2ab;
  font-size: 12px;
  padding: 0 10px;
}
)CSS";
}

}  // namespace moe::bridge
