import os
from pathlib import Path
import docker


DOCKER_CLIENT = docker.from_env()
ROOT_DIR = Path(__file__).parents[2]
HOUDINI_VERSIONS = [ '21.0.559','20.5.684', '20.0.896' ]
DOCKERFILES_WINDOWS = ROOT_DIR / 'docker' / 'windows'
IMG_REV = 'v0'
IMG_NAME_BASE = 'palladio-tc-base'
IMG_NAME = 'palladio-tc'
TAG_PREFIX = 'win19-vc1438'


def main():
	base_dockerfile = DOCKERFILES_WINDOWS / 'Dockerfile-base'
	base_image_name = f'{IMG_NAME_BASE}:{TAG_PREFIX}-{IMG_REV}'
	base_build_context = str(ROOT_DIR)
	print(f'>>> building base image "{base_image_name}" from "{base_dockerfile}" at "{base_build_context}"')
	DOCKER_CLIENT.images.build(dockerfile=str(base_dockerfile), path=base_build_context, tag=base_image_name)

	dockerfile = DOCKERFILES_WINDOWS / 'Dockerfile-houdini'
	for houdini_version in HOUDINI_VERSIONS:
		tag_suffix_hdk = f'hdk{houdini_version.replace('.', '')}'
		image_name = f'{IMG_NAME}:{TAG_PREFIX}-{tag_suffix_hdk}-{IMG_REV}'
		build_context = get_houdini_installation_path(houdini_version)
		print(f'>>> building image "{image_name}" for Houdini {houdini_version} from "{dockerfile}" at "{build_context}" ...')
		DOCKER_CLIENT.images.build(dockerfile=str(dockerfile), path=str(build_context), tag=image_name,
							 buildargs={ 'BASE_IMAGE': base_image_name, 'HOUDINI_VERSION': houdini_version })


def get_houdini_installation_path(houdini_ver):
    return Path(f'C:/Program Files/Side Effects Software/Houdini {houdini_ver}')


if __name__ == '__main__':
	main()