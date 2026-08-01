#include "src/parent/worktree/worktree_helper_commands.h"

#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {

std::vector<char*> mutable_arguments(std::vector<std::string>& arguments) {
  std::vector<char*> result;
  result.reserve(arguments.size());
  for (std::string& argument : arguments) {
    result.push_back(argument.data());
  }
  return result;
}

TEST(WorktreeHelperCommandsTest, RepositoryRegistrationRejectsInvalidArgumentCount) {
  std::vector<std::string> storage{
      "workspace_parent",
      "--register-worktree-repository",
      "/tmp/worktrees.pb",
  };
  std::vector<char*> const arguments = mutable_arguments(storage);
  std::ostringstream output;
  std::ostringstream error_output;

  EXPECT_EQ(moe::parent::run_worktree_repository_registration_command(
                arguments, {.standard_output = output, .error_output = error_output}),
            2);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(error_output.str(),
            "usage: workspace_parent --register-worktree-repository "
            "<registry-path> <repository-root> [clone-url]\n");
}

TEST(WorktreeHelperCommandsTest, WorktreeProvisionRejectsInvalidArgumentCount) {
  std::vector<std::string> storage{
      "workspace_parent", "--provision-worktree", "/tmp/worktrees.pb", "/tmp/repository", "branch",
  };
  std::vector<char*> const arguments = mutable_arguments(storage);
  std::ostringstream output;
  std::ostringstream error_output;

  EXPECT_EQ(moe::parent::run_worktree_provision_command(
                arguments, {.standard_output = output, .error_output = error_output}),
            2);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(error_output.str(),
            "usage: workspace_parent --provision-worktree "
            "<registry-path> <repository-root> <branch> <worktree-path>\n");
}

TEST(WorktreeHelperCommandsTest, RepositoryRegistrationReportsValidationFailure) {
  std::vector<std::string> storage{
      "workspace_parent",
      "--register-worktree-repository",
      "/tmp/worktrees.pb",
      "relative-repository",
  };
  std::vector<char*> const arguments = mutable_arguments(storage);
  std::ostringstream output;
  std::ostringstream error_output;

  EXPECT_EQ(moe::parent::run_worktree_repository_registration_command(
                arguments, {.standard_output = output, .error_output = error_output}),
            1);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(error_output.str(),
            "Repository registration failed: repository root must be an absolute path\n");
}

TEST(WorktreeHelperCommandsTest, WorktreeProvisionReportsValidationFailure) {
  std::vector<std::string> storage{
      "workspace_parent",
      "--provision-worktree",
      "/tmp/worktrees.pb",
      "/tmp/repository",
      "",
      "/tmp/repository/worktree",
  };
  std::vector<char*> const arguments = mutable_arguments(storage);
  std::ostringstream output;
  std::ostringstream error_output;

  EXPECT_EQ(moe::parent::run_worktree_provision_command(
                arguments, {.standard_output = output, .error_output = error_output}),
            1);
  EXPECT_TRUE(output.str().empty());
  EXPECT_EQ(error_output.str(), "Worktree operation failed: Branch must not be empty\n");
}

}  // namespace
