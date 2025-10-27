#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>
#include <assert.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>


void init_glfw_or_crash()
{
	if (glfwInit() != GLFW_TRUE) {
		fprintf(stderr, "glfwInit\n");
		exit(1);
	}
}

GLFWwindow *init_window_or_crash(int width, int height, const char *title)
{
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow *win = glfwCreateWindow(width, height, title, NULL, NULL);
	if (!win) {
		fprintf(stderr, "glfwCreateWindow\n");
		exit(1);
	}
	return win;
}

VkInstance vulkan_instance_or_crash()
{
	VkApplicationInfo app_desc = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "Gala",
		.applicationVersion = VK_MAKE_VERSION(0, 0, 0),
		.pEngineName = "No engine",
		.engineVersion = VK_MAKE_VERSION(0, 0, 0),
		.apiVersion = VK_API_VERSION_1_0,
	};
	VkInstanceCreateInfo inst_desc = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_desc,
		.enabledLayerCount = 0,
	};
	inst_desc.ppEnabledExtensionNames = glfwGetRequiredInstanceExtensions(&inst_desc.enabledExtensionCount);
	VkInstance inst;
	VkResult status = vkCreateInstance(&inst_desc, NULL, &inst);
	if (status != VK_SUCCESS) {
		fprintf(stderr, "vkCreateInstance\n");
		exit(1);
	}
	return inst;
}

static const int WIDTH = 800;
static const int HEIGHT = 600;

int main()
{
	init_glfw_or_crash();
	GLFWwindow *win = init_window_or_crash(WIDTH, HEIGHT, "Gala");
	uint32_t n_ext;
	vkEnumerateInstanceExtensionProperties(NULL, &n_ext, NULL);
	printf("%" PRIu32 " extensions supported for Vulkan\n", n_ext);
	VkInstance inst = vulkan_instance_or_crash();
	(void) inst;
	while (!glfwWindowShouldClose(win)) {
		glfwPollEvents();
	}
	glfwDestroyWindow(win);
	glfwTerminate();
	return 0;
}

