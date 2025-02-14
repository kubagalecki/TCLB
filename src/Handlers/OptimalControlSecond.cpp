#include "OptimalControlSecond.h"

std::string OptimalControlSecond::xmlname = "OptimalControlSecond";

int OptimalControlSecond::Init () {
		std::string par;
		std::string zone;
		zone_number = -10;
		par_index = -10;
		Pars = -1;
		pugi::xml_attribute attr = node.attribute("what");
		if (attr) {
	                par = attr.value();
                        size_t i = par.find_first_of('-');
                        if (i == string::npos) {
				ERROR("Can only optimal control a parameters in a specific zone\n");
				return -1;
                        } else {
                                zone = par.substr(i+1);
                                par = par.substr(0,i);
                                if (solver->geometry->SettingZones.count(zone) > 0) { 
                                        zone_number = solver->geometry->SettingZones[zone];
                                } else {
                                        ERROR("Unknown zone %s (found while setting parameter %s)\n", zone.c_str(), par.c_str());
					return -1;
                                }
                        }
			const Model::ZoneSetting& it = solver->lattice->model->zonesettings.by_name(par);
			if (!it) {
				error("Unknown param %s in OptimalControl\n", par.c_str());
				return -1;
			}
			par_index = it.id;
			output("Selected %s (%d) in zone \"%s\" (%d) for optimal control\n", par.c_str(), par_index, zone.c_str(), zone_number);
		} else {
			ERROR("Parameter \"what\" needed in %s\n",node.name());
			return -1;
		}
		Pars2 = solver->lattice->zSet.getLen(par_index, zone_number);
		Pars = Pars2 / 2;
		tab2 = new double[Pars2];
		output("Lenght of the control: %d\n", Pars);
		old_iter_type = solver->iter_type;
		solver->iter_type |= ITER_GLOBS;

		attr = node.attribute("lower");
		if (attr) {
			lower = solver->units.alt(attr.value());
		} else {
			notice("lower bound not set in %s - setting to -1\n",node.name());
			lower = -1;
		}
		attr = node.attribute("upper");
		if (attr) {
			upper = solver->units.alt(attr.value());
		} else {
			notice("upper bound not set in %s - setting to 1\n",node.name());
			upper = 1;
		}
		f = fopen((std::string(solver->info.outpath) + "_OC_" + par + "_" + zone + ".dat").c_str(),"w");
		assert( f != NULL );
		return Design::Init();
	};


int OptimalControlSecond::NumberOfParameters () {
		return Pars;
	};


int OptimalControlSecond::Parameters (int type, double * tab) {
		switch(type) {
		case PAR_GET:
			output("Getting the params from the zone\n");
			solver->lattice->zSet.get(par_index, zone_number, tab2);
			for (int i=0; i<Pars2;i+=2) tab[i/2]=tab2[i];
			fprintf(f,"GET");
			for (int i=0;i<Pars;i++) fprintf(f,",%lg",(double) tab[i]);
			fprintf(f,"\n"); fflush(f);
			return 0;
		case PAR_SET:
			output("Setting the params in the zone\n");
			fprintf(f,"SET");
			for (int i=0;i<Pars;i++) fprintf(f,",%lg",(double) tab[i]);
			fprintf(f,"\n"); fflush(f);
			for (int i=0; i<Pars;i++) {
				if (2*i<Pars2) tab2[2*i] = tab[i];
				if (2*i+1<Pars2) {
					if (i+1<Pars) tab2[2*i+1]=(tab[i]+tab[i+1])/2;
					else tab2[2*i+1]=tab[i];
				}
			}
			solver->lattice->zSet.set(par_index, zone_number, tab2);
			return 0;
		case PAR_GRAD:
			output("Getting gradient of a param (%d) in zone(%d)\n", par_index, zone_number);
			solver->lattice->zSet.get_grad(par_index, zone_number, tab2);
			for (int i=0; i<Pars;i++) tab[i]=0;
			for (int i=0; i<Pars;i++) {
				if (2*i<Pars2) tab[i] += tab2[2*i];
				if (2*i+1<Pars2) {
					if (i+1<Pars) {
						tab[i] += tab2[2*i+1]/2;
						tab[i+1] += tab2[2*i+1]/2;
					}
					else tab[i] += tab2[2*i+1];
				}
			}
			fprintf(f,"GRAD");
			for (int i=0;i<Pars;i++) fprintf(f,",%lg",(double) tab[i]);
			fprintf(f,"\n"); fflush(f);
			return 0;
		case PAR_UPPER:
			for (int i=0;i<Pars;i++) tab[i]=upper;
			return 0;
		case PAR_LOWER:
			for (int i=0;i<Pars;i++) tab[i]=lower;
			return 0;
		default:
			ERROR("Unknown type %d in call to Parameters in %s\n", type, node.name());
			exit(-1);
		}
	};


// Register the handler (basing on xmlname) in the Handler Factory
template class HandlerFactory::Register< GenericAsk< OptimalControlSecond > >;
