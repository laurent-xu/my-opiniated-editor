load("@rules_nixpkgs_core//:nixpkgs.bzl", "nixpkgs_http_repository", "nixpkgs_package")
load("@rules_nixpkgs_python//:python.bzl", "nixpkgs_python_configure")

_NIXPKGS_24_05_SHA256 = "911314b81780f26fdaf87e17174210bdbd40c86bac1795212f257cdc236a1e78"

_RUFF_BUILD = """
package(default_visibility = ["//visibility:public"])

filegroup(
    name = "bin",
    srcs = glob(["bin/*"]),
)

sh_binary(
    name = "ruff",
    srcs = ["bin/ruff"],
)
"""

def _nix_deps_impl(_ctx):
    nixpkgs_http_repository(
        name = "nixpkgs",
        url = "https://github.com/NixOS/nixpkgs/archive/refs/tags/24.05.tar.gz",
        sha256 = _NIXPKGS_24_05_SHA256,
        strip_prefix = "nixpkgs-24.05",
    )

    nixpkgs_python_configure(
        name = "nixpkgs_python_toolchain",
        python3_attribute_path = "python3",
        repository = "@nixpkgs",
        register = False,
    )

    nixpkgs_package(
        name = "nixpkgs_ruff",
        attribute_path = "ruff",
        repository = "@nixpkgs",
        build_file_content = _RUFF_BUILD,
    )

nix_deps = module_extension(implementation = _nix_deps_impl)
