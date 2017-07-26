#define FUSE_USE_VERSION 31

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <assert.h>

#include <string>
#include <wfslib/WfsLib.h>

static std::shared_ptr<Wfs> wfs;

static int wfs_getattr(const char *path, struct stat *stbuf) {
	memset(stbuf, 0, sizeof(struct stat));

	auto item = wfs->GetObject(path);
	if (!item) return -ENOENT;
	if (item->IsDirectory()) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2 + std::dynamic_pointer_cast<Directory>(item)->GetItemsCount();
	}
	else if (item->IsLink()) {
		stbuf->st_mode = S_IFLNK | 0777;
		stbuf->st_nlink = 1;
		stbuf->st_size = 0; // TODO
	}
	else if (item->IsFile()) {
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = std::dynamic_pointer_cast<File>(item)->GetSize();
	} else {
		// Should not happen
		return -ENOENT;
	}

	return 0;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi) {
	(void) offset;
	(void) fi;

	auto item = wfs->GetObject(path);
	if (!item || !item->IsDirectory())
		return -ENOENT;

	filler(buf, ".", NULL, 0);
	filler(buf, "..", NULL, 0);

	for (auto item : *std::dynamic_pointer_cast<Directory>(item)) {
		filler(buf, item->GetName().c_str(), NULL, 0);
	}

	return 0;
}

static int wfs_open(const char *path, struct fuse_file_info *fi) {
	if (!wfs->GetObject(path))
		return -ENOENT;

	if ((fi->flags & O_ACCMODE) != O_RDONLY)
		return -EACCES;

	    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi) {
	(void) fi;

	auto item = wfs->GetObject(path);
	if (!item || !item->IsFile())
		return -ENOENT;

	File::stream stream(std::dynamic_pointer_cast<File>(item));

	stream.seekg(offset, stream.beg);
	stream.read(reinterpret_cast<unsigned char*>(buf), size);

	return static_cast<int>(stream.gcount());
}

int wfs_readlink(const char *path, char *buf, size_t size) {
	// TODO
	auto item = wfs->GetObject(path);
	if (!item || !item->IsLink())
		return -ENOENT;

	// TODO
        return -ENOENT;
}

static const char *usage =
"usage: wfs-fuse <device_file> <mountpoint> --otp OTP_PATH [--seeprom SEEPROM_PATH] [--usb] [--mlc]\n"
"\n"
"options:\n"
"    --help|-h             print this help message\n"
"    --otp|-o PATH         otp file\n"
"    --seeprom|-s PATHH    seeprom file\n"
"    --usb|-u              device is usb (default)\n"
"    --mlc|-m              device is mlc\n"
"    -d   -o debug         enable debug output (implies -f)\n"
"    -f                    foreground operation\n"
"    -s                    disable multi-threaded operation\n"
"\n";

struct wfs_param {
	char			*file;
	char			*otp;
	char			*seeprom;
	int			is_usb;
	int			is_mlc;
	int			is_help;
};

#define WFS_OPT(t, p) { t, offsetof(struct wfs_param, p), 1 }

static const struct fuse_opt wfs_opts[] = {
	WFS_OPT("-o %s",		otp),
	WFS_OPT("--otp %s",		otp),
	WFS_OPT("-s %s",		seeprom),
	WFS_OPT("--seeprom %s",		seeprom),
	WFS_OPT("-u",			is_usb),
	WFS_OPT("--usb",		is_usb),
	WFS_OPT("-m",			is_mlc),
	WFS_OPT("--mlc",		is_mlc),
	FUSE_OPT_KEY("-h",		0),
	FUSE_OPT_KEY("--help",		0),
	FUSE_OPT_END
};

static int wfs_process_arg(void *data, const char *arg, int key,
			   struct fuse_args *outargs) {
	struct wfs_param *param = static_cast<wfs_param*>(data);

	(void)outargs;
	(void)arg;

	switch (key) {
	case 0:
		param->is_help = 1;
		fprintf(stderr, "%s", usage);
		return fuse_opt_add_arg(outargs, "-ho");

	case FUSE_OPT_KEY_NONOPT:
		if( !param->file ) {
			param->file = strdup(arg);
			return 0;
		}
		return 1;
	default:
		return 1;
	}
}

int main(int argc, char *argv[]) {
 	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct wfs_param param = { NULL, NULL, NULL, 0, 0, 0 };

	if (fuse_opt_parse(&args, &param, wfs_opts, wfs_process_arg)) {
		printf("failed to parse option\n");
		return 1;
	}

	if (param.is_help) {
		return 0;
	}
	if (!param.file) {
		printf("Missing file (--file)\n");
		return 1;
	}
	if (!param.otp) {
		printf("Missing otp file (--otp)\n");
		return 1;
	}
	if (!param.is_mlc && !param.seeprom) {
		printf("Missing seeprom file (--seeprom)\n");
		return 1;
	}
	if (param.is_usb && param.is_mlc) {
		printf("Can't specify both --mlc and --usb\n");
		return 1;
	}

	try {
		std::vector<uint8_t> key;
		std::unique_ptr<OTP> otp;
		// open otp
		try {
			otp.reset(OTP::LoadFromFile(param.otp));
		}
		catch (std::exception& e) {
			std::cerr << "Failed to open OTP: " << e.what() << std::endl;
			return 1;
		}

		if (param.is_mlc) {
			// mlc
			key = otp->GetMLCKey();
		}
		else {
			// usb
			std::unique_ptr<SEEPROM> seeprom;
			try {
				seeprom.reset(SEEPROM::LoadFromFile(param.seeprom));
			}
			catch (std::exception& e) {
				std::cerr << "Failed to open SEEPROM: " << e.what() << std::endl;
				return 1;
			}
			key = seeprom->GetUSBKey(*otp);
		}
		auto device = std::make_shared<FileDevice>(param.file, 9);
		Wfs::DetectSectorsCount(device, key);
		Wfs::DetectSectorSize(device, key);
		wfs.reset(new Wfs(device, key));
	}
	catch (std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}

	struct fuse_operations wfs_oper = {0};
	wfs_oper.getattr	= wfs_getattr;
	wfs_oper.readdir	= wfs_readdir;
	wfs_oper.open		= wfs_open;
	wfs_oper.read		= wfs_read;
	wfs_oper.readlink	= wfs_readlink;

	return fuse_main(args.argc, args.argv, &wfs_oper, NULL);

}
